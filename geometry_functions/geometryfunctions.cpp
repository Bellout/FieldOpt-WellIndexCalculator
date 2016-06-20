#include "geometryfunctions.h"

namespace WellIndexCalculator {
    namespace GeometryFunctions {
        using namespace Eigen;

        Vector3d line_plane_intersection(Vector3d p0, Vector3d p1, Vector3d normal_vector, Vector3d point_in_plane) {
            Vector3d line_vector = p1 - p0;
            line_vector.normalize();
            normal_vector.normalize();
            auto w = p0 - point_in_plane;
            double s = normal_vector.dot(-w) / normal_vector.dot(line_vector);
            return p0 + s*line_vector;
        }

        bool point_on_same_side(Vector3d point, Vector3d plane_point, Vector3d normal_vector, double slack) {
            double dot_product = (point - plane_point).dot(normal_vector);
            return dot_product >= 0.0 - slack;
        }

        QList<Reservoir::WellIndexCalculation::IntersectedCell> cells_intersected(Vector3d start_point, Vector3d end_point,
                                                                                  Reservoir::Grid::Grid *grid) {
            using namespace Reservoir::WellIndexCalculation;

            QList<IntersectedCell> cells;
            // Add first and last cell blocks to the lists
            Reservoir::Grid::Cell last_cell = grid->GetCellEnvelopingPoint(end_point);
            Reservoir::Grid::Cell first_cell = grid->GetCellEnvelopingPoint(start_point);
            cells.append(IntersectedCell(first_cell));
            cells[0].set_entry_point(start_point);


            // If the first and last blocks are the same, return the block and start+end points
            if (last_cell.global_index() == first_cell.global_index()) {
                cells[0].set_exit_point(end_point);
                return cells;
            }

            Vector3d exit_point = find_exit_point(first_cell, start_point, end_point, start_point);
            // Make sure we follow line in the correct direction. (i.e. dot product positive)
            if ((end_point - start_point).dot(exit_point - start_point) <= 0.0) {
                exit_point = find_exit_point(first_cell, start_point, end_point, exit_point);
            }

            double epsilon = 0.01 / (end_point - exit_point).norm();
            Vector3d move_exit_epsilon = exit_point * (1 - epsilon) + end_point * epsilon;
            Reservoir::Grid::Cell current_cell = grid->GetCellEnvelopingPoint(move_exit_epsilon);
            double epsilon_temp = epsilon;

            // Move untill we're out of the first cell
            while (current_cell.global_index() == first_cell.global_index()) {
                epsilon_temp = 10 * epsilon_temp;
                move_exit_epsilon = exit_point * (1 - epsilon_temp) + end_point * epsilon_temp;
                current_cell = grid->GetCellEnvelopingPoint(move_exit_epsilon);
            }
            cells[0].set_exit_point(exit_point);

            // Add previous exit point to list, find next exit point and all other up to the end_point
            while (current_cell.global_index() != last_cell.global_index()) {
                IntersectedCell next_cell = IntersectedCell(current_cell); // Add the previously located cell
                next_cell.set_entry_point(exit_point);

                exit_point = find_exit_point(current_cell, exit_point, end_point, exit_point);
                next_cell.set_exit_point(exit_point);
                cells.append(next_cell);

                if (exit_point == end_point) { // Terminate loop if the exit point is the last point
                    current_cell = last_cell;
                }
                else { // Move slightly along line to enter the new cell and get it
                    move_exit_epsilon = exit_point * (1 - epsilon) + end_point * epsilon;
                    current_cell = grid->GetCellEnvelopingPoint(move_exit_epsilon);
                }
            }
            cells.append(IntersectedCell(last_cell));
            cells.last().set_entry_point(exit_point);
            cells.last().set_exit_point(end_point);
            assert(cells.last().global_index() == last_cell.global_index());
            return cells;
        }

        Vector3d find_exit_point(Reservoir::Grid::Cell cell,
                                 Vector3d entry_point, Vector3d end_point, Vector3d exception_point) {
            Vector3d line = end_point - entry_point;

            // Loop through the cell faces untill we find one that the line intersects
            for (Reservoir::Grid::Cell::Face face : cell.faces()) {
                if (face.normal_vector.dot(line) != 0) { // Check that the line and face are not parallel.
                    auto intersect_point = line_plane_intersection(entry_point, end_point, face.normal_vector, face.corners[0]);

                    // Check that the intersect point is on the correct side of all faces (i.e. inside the cell)
                    bool feasible_point = true;
                    for (auto p : cell.faces()) {
                        if (!point_on_same_side(intersect_point, p.corners[0], p.normal_vector, 10e-6)) {
                            feasible_point = false;
                            break;
                        }
                    }

                    // Return the point if it is deemed feasible, not identical to the entry point, and going in the correct direction.
                    if (feasible_point && (exception_point - intersect_point).norm() > 10e-10
                        && (end_point - entry_point).dot(end_point - intersect_point) >= 0) {
                        return intersect_point;
                    }
                }
            }
            // If all fails, the line intersects the cell in a single point (corner or edge) -> return entry_point
            return entry_point;
        }

        double well_index_cell(Reservoir::WellIndexCalculation::IntersectedCell icell, double wellbore_radius) {
            double Lx = 0;
            double Ly = 0;
            double Lz = 0;

            for (int ii = 0; ii < icell.points().length() - 1; ++ii) { // Current segment ii
                // Compute vector from segment
                Vector3d current_vec = icell.points().at(ii+1) - icell.points().at(ii);

                /* Proejcts segment vector to directional spanning vectors and determines the length.
                 * of the projections. Note that we only only care about the length of the projection,
                 * not the spatial position. Also adds the lengths of previous segments in case there
                 * is more than one segment within the well.
                 */
                Lx = Lx + (icell.xvec() * icell.xvec().dot(current_vec) / icell.xvec().dot(icell.xvec())).norm();
                Ly = Ly + (icell.yvec() * icell.yvec().dot(current_vec) / icell.yvec().dot(icell.yvec())).norm();
                Lz = Lz + (icell.zvec() * icell.zvec().dot(current_vec) / icell.zvec().dot(icell.zvec())).norm();
            }

            // Compute Well Index from formula provided by Shu
            double well_index_x = (dir_well_index(Lx, icell.dy(), icell.dz(), icell.permy(), icell.permz(), wellbore_radius));
            double well_index_y = (dir_well_index(Ly, icell.dx(), icell.dz(), icell.permx(), icell.permz(), wellbore_radius));
            double well_index_z = (dir_well_index(Lz, icell.dx(), icell.dy(), icell.permx(), icell.permy(), wellbore_radius));
            return sqrt(well_index_x * well_index_x + well_index_y * well_index_y + well_index_z * well_index_z);
        }

        double dir_well_index(double Lx, double dy, double dz, double ky, double kz, double wellbore_radius) {
            double silly_eclipse_factor = 0.008527;
            double well_index_i = silly_eclipse_factor * (2 * M_PI * sqrt(ky * kz) * Lx) /
                                  (log(dir_wellblock_radius(dy, dz, ky, kz) / wellbore_radius));
            return well_index_i;
        }

        double dir_wellblock_radius(double dx, double dy, double kx, double ky) {
            double r = 0.28 * sqrt((dx * dx) * sqrt(ky / kx) + (dy * dy) * sqrt(kx / ky)) /
                       (sqrt(sqrt(kx / ky)) + sqrt(sqrt(ky / kx)));
            return r;
        }

        QList<Reservoir::WellIndexCalculation::IntersectedCell> well_index_of_grid(Reservoir::Grid::Grid *grid,
                                                                                   QList<Eigen::Vector3d> well_spline_points,
                                                                                   double wellbore_radius) {
            assert(well_spline_points.length() == 2);
            // Find intersected blocks and the points of intersection
            // \todo Call this in a loop. When there is more than two ponts defining the spline, ensure that the blocks are not duplicated when going from one segment to the next.
            QList<Reservoir::WellIndexCalculation::IntersectedCell> intersected_cells = cells_intersected(
                    well_spline_points.first(), well_spline_points.last(), grid);

            for (auto cell : intersected_cells) {
                cell.set_well_index(well_index_cell(cell, wellbore_radius));
            }
            return intersected_cells;
        }

    }
}
