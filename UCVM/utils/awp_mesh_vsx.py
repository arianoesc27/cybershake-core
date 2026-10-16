#!/usr/bin/env python3
#AR: New script (2026-07-16), part of the stochastic-codes extension's "generic" (non-UCVM)
#AR: velocity-info backend (see cybershake_workflows/steps/stochastic/base.py's awp_mesh_vsx()).
#AR:
#AR: It computes the "adjusted VsX" that LF Site Response needs. retrieve_vs.c calls it VsD5H,
#AR: and it is the slowness average of Vs from the surface down to 5 times the grid spacing.
#AR: Here it is read from AWP's OWN velocity mesh file, not from UCVM or from the Vs30 grid
#AR: (vs30_from_grid.py). That matters because the LF seismogram was computed on AWP's coarse
#AR: mesh, so the site response correction has to match what THAT mesh had near the surface.
#AR:
#AR: Reuses the existing, already-tested building blocks in this same directory rather than
#AR: reimplementing a mesh reader from scratch:
#AR: - nearest_grid_point_using_coords.py's brute-force lon/lat -> (x_index, y_index) approach
#AR:   (here via pyproj.Geod, matching the pattern already used in my own
#AR:   VelocityModel_utils.py, rather than requiring a UTM zone argument).
#AR: - the mesh format as value_from_mesh.c reads it (flat float32 binary, 3 floats per point,
#AR:   Vp, Vs and density). I checked the read loop in AWP-GPU-SGT/src/mesh.c, and for the
#AR:   single-file case the offset is z*(NY*NX) + y*NX + x (X fastest, then Y, then Z).
#AR:
#AR: NX/NY/NZ are parsed directly from the mesh filename itself (e.g.
#AR: "Iceland_1200x600x256_Dh200m.bin" -> NX=1200, NY=600, NZ=256), matching the exact naming
#AR: convention my own VelocityModel_utils.py (interpolate_and_save_streaming /
#AR: interpolate_1d_and_save_streaming / save_binary_model) already uses. That way I don't have
#AR: to parse the gridout file, which is harder (whose "x"/"y" section labels are themselves swapped
#AR: relative to AWP's own convention, per preAWP/base.py's comments).
#AR:
#AR: The near-surface average uses z indices 0 to 4 (5 layers of AWP's mesh) directly, with no
#AR: unit conversion. AWP's mesh spacing IS the grid spacing, so "5 * gridspacing" is simply
#AR: "5 layers". That is the same Vs5H/VsD5H definition as in retrieve_vs.c (a depth range of
#AR: 5*gridspacing), applied to AWP's mesh instead of a UCVM query.
"""
Usage: awp_mesh_vsx.py <mesh_file> <model_coords_file> <lon> <lat> <gridspacing_m> <outfile>

<mesh_file>: AWP's own velocity-mesh binary (e.g. Iceland_1200x600x256_Dh200m.bin).
<model_coords_file>: the region's *.model_coords_<region> file (lon lat x_index y_index rows).
<lon>, <lat>: site coordinates (decimal degrees).
<gridspacing_m>: unused directly (AWP's mesh spacing already defines the 5-layer window) --
    kept as an argument for interface symmetry with velocity_info_ucvm/vs30_from_grid.
<outfile>: output text file, one line "VsX = <value>".
"""
import os
import re
import struct
import sys
from pyproj import Geod

NUM_SURFACE_LAYERS = 5  # matches VsD5H's own "5 * gridspacing" definition
NVAR = 3  # Vp, Vs, density (elastic, no Qp/Qs), checked on the Iceland mesh file


def _parse_mesh_dims(mesh_file):
    match = re.search(r"_(\d+)x(\d+)x(\d+)_Dh", os.path.basename(mesh_file))
    if not match:
        raise ValueError(
            "Could not parse NX/NY/NZ from mesh filename '%s' (expected a "
            "'<region>_<NX>x<NY>x<NZ>_Dh<spacing>m.bin' pattern)" % mesh_file)
    return int(match.group(1)), int(match.group(2)), int(match.group(3))


def _nearest_grid_point(model_coords_file, lon, lat):
    geod = Geod(ellps="WGS84")
    closest_dist = float("inf")
    closest_x, closest_y = None, None
    with open(model_coords_file, "r") as fp_in:
        for line in fp_in:
            parts = line.split()
            if len(parts) != 4:
                continue
            test_lon, test_lat = float(parts[0]), float(parts[1])
            if abs(lat - test_lat) > 0.05 or abs(lon - test_lon) > 0.05:
                continue
            _, _, dist = geod.inv(lon, lat, test_lon, test_lat)
            if dist < closest_dist:
                closest_dist = dist
                closest_x, closest_y = int(parts[2]), int(parts[3])
    if closest_x is None:
        raise ValueError(
            "No grid point within 0.05 degrees of (lon=%f, lat=%f) found in %s" % (
                lon, lat, model_coords_file))
    return closest_x, closest_y


def _read_vs(mesh_file, nx, ny, x_index, y_index, z_index):
    flat_index = z_index * (ny * nx) + y_index * nx + x_index
    byte_offset = flat_index * NVAR * 4
    with open(mesh_file, "rb") as f:
        f.seek(byte_offset)
        vp, vs, rho = struct.unpack("<3f", f.read(12))
    return vs


def main():
    if len(sys.argv) != 7:
        sys.stderr.write(
            "Usage: %s <mesh_file> <model_coords_file> <lon> <lat> <gridspacing_m> <outfile>\n"
            % sys.argv[0])
        sys.exit(1)

    mesh_file, model_coords_file, lon_str, lat_str, _gridspacing_m, outfile = sys.argv[1:7]
    lon, lat = float(lon_str), float(lat_str)

    nx, ny, nz = _parse_mesh_dims(mesh_file)
    x_index, y_index = _nearest_grid_point(model_coords_file, lon, lat)

    num_layers = min(NUM_SURFACE_LAYERS, nz)
    vs_samples = [
        _read_vs(mesh_file, nx, ny, x_index, y_index, z)
        for z in range(num_layers)
    ]

    #AR: Same slowness/harmonic-weighted average retrieve_vs.c's ucvm_vsD/ucvm_vs_discrete
    #AR: use. The two ends weigh 0.5, the inner layers weigh 1.0, and the result is
    #AR: (num_intervals) / (weighted sum of 1/Vs).
    num_intervals = num_layers - 1
    weighted_slowness = 0.0
    for i, vs in enumerate(vs_samples):
        weight = 0.5 if (i == 0 or i == num_layers - 1) else 1.0
        weighted_slowness += weight / vs
    vsx = num_intervals / weighted_slowness if num_intervals > 0 else vs_samples[0]

    with open(outfile, "w") as f:
        f.write("VsX = %.1f\n" % vsx)


if __name__ == "__main__":
    main()
