#!/usr/bin/env python3
#AR: New script (2026-10-14). The three velocity values of a site that the low-frequency site
#AR: response needs, for a region without UCVM. It writes the same three lines retrieve_vs
#AR: writes for California,
#AR:
#AR:   Vs30   = Vs averaged in travel time over the top 30 m of the velocity model
#AR:   Vs<5h> = the same over the top 5 grid spacings of the velocity model
#AR:   VsD<5h> = the same over the top 5 grid spacings as the AWP mesh sees them
#AR:
#AR: and the workflow then sets vref = Vs30 x VsD5H / Vs5H, as SCEC's workflow does
#AR: (cybershake-tools, CyberShake_Sub_Stoch_DAXGen.java, createLFSiteResponseJob).
#AR:
#AR: Vs30 and Vs5H follow ucvm_vsD in retrieve_vs.c, one value every metre from 0.5 m down, no
#AR: minimum Vs. They are read from the NetCDF velocity model of the region (the vmFile the
#AR: interpolator builds the mesh from), nearest point in plan and linear in depth. Without a
#AR: NetCDF the mesh itself is taken as the model, interpolated linearly between its layers, and
#AR: then VsD5H / Vs5H is close to 1 and vref is close to the Vs30 of the mesh.
#AR:
#AR: VsD5H follows ucvm_vs_discrete in retrieve_vs.c, the mesh layers 0 to 5, half weight on the
#AR: first and the last. It is read from the AWP mesh file itself, so it already carries the
#AR: minimum Vs and the surface depth the mesh was built with.
"""
Usage: site_vs_profile.py <mesh_file> <model_coords_file> <lon> <lat> <gridspacing_m> <outfile>
                          [--dims NXxNYxNZ] [--netcdf FILE --units m/s|km/s]

<mesh_file>: the AWP mesh, 3 floats per point (Vp, Vs, density).
<model_coords_file>: lon lat x_index y_index of every mesh column.
<gridspacing_m>: the grid spacing of the mesh in metres.
--dims: number of x indices, of y indices and of layers. Without it they come from the largest
        indices of the coordinates file and NZ from the size of the mesh.
--netcdf, --units: the NetCDF velocity model (latitude, longitude, depth in km positive down,
        vs) and the units of its vs.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "UCVM", "utils"))
from awp_mesh_vsx import _nearest_grid_point  # noqa: E402

NUM_INTERVALS = 5  # 5h, as in retrieve_vs.c


def travel_time_average(depths_m, vs_of_depth):
    #AR: Same as ucvm_vsD, the depth range divided by the sum of 1/Vs at every metre.
    vs = np.asarray([vs_of_depth(z) for z in depths_m], dtype=float)
    return len(depths_m) / np.sum(1.0 / vs)


def netcdf_profile(netcdf_file, lon, lat, units):
    import xarray as xr
    ds = xr.open_dataset(netcdf_file)
    column = ds["vs"].sel(longitude=lon, latitude=lat, method="nearest")
    depth_m = np.asarray(ds["depth"].values, dtype=float) * 1000.0
    vs = np.asarray(column.transpose("depth").values, dtype=float)
    if units == "km/s":
        vs = vs * 1000.0
    elif units != "m/s":
        raise ValueError("velocity_units must be m/s or km/s, got %s" % units)
    order = np.argsort(depth_m)
    depth_m, vs = depth_m[order], vs[order]
    #AR: (2026-10-15) The depth step of the model at the top, returned so main can warn when it
    #AR: is coarser than the 30 m of Vs30.
    top_step = float(depth_m[1] - depth_m[0]) if len(depth_m) > 1 else float("inf")
    return (lambda z: float(np.interp(z, depth_m, vs))), top_step


def read_column(mesh_file, n_x, n_y, x_index, y_index, n_layers):
    #AR: The y index of the coordinates file is the fastest axis of the mesh file, then x, then
    #AR: depth. Checked on the PAS mesh of VelMesh against its NetCDF, 300 random columns, the
    #AR: surface layer matches with correlation 1.000 and a median difference of 4 m/s in this
    #AR: order and does not match at all with x fastest. (awp_mesh_vsx.py reads it with x
    #AR: fastest. On the Iceland mesh, built from a 1D model, every column is the same, so its
    #AR: values were right anyway.)
    values = []
    with open(mesh_file, "rb") as f:
        for z in range(n_layers):
            flat = z * (n_x * n_y) + x_index * n_y + y_index
            f.seek(flat * 12 + 4)
            values.append(float(np.frombuffer(f.read(4), dtype="<f4")[0]))
    return values


def mesh_dims_from_coords(mesh_file, coords_file):
    #AR: The number of x and y indices, from the largest of each in the
    #AR: coordinates file, and NZ from the size of the mesh. The IN3D gives NX and NY swapped
    #AR: (see build_IN3D_GPU, "remember, X and Y are flipped"), and a per-site mesh has no
    #AR: dimensions in its name, so the coordinates file is the safe source.
    max_x = max_y = -1
    with open(coords_file) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 4:
                max_x = max(max_x, int(parts[2]))
                max_y = max(max_y, int(parts[3]))
    nx, ny = max_x + 1, max_y + 1
    nz = os.path.getsize(mesh_file) // (nx * ny * 12)
    return nx, ny, nz


def main():
    import argparse
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("mesh_file")
    ap.add_argument("coords_file")
    ap.add_argument("lon", type=float)
    ap.add_argument("lat", type=float)
    ap.add_argument("gridspacing_m", type=float)
    ap.add_argument("outfile")
    ap.add_argument("--dims")
    ap.add_argument("--netcdf")
    ap.add_argument("--units", default="m/s")
    a = ap.parse_args()
    h = int(round(a.gridspacing_m))

    if a.dims:
        nx, ny, nz = (int(v) for v in a.dims.lower().split("x"))
    else:
        nx, ny, nz = mesh_dims_from_coords(a.mesh_file, a.coords_file)
    expected = nx * ny * nz * 12
    if os.path.getsize(a.mesh_file) != expected:
        raise ValueError("%s has %d bytes, %dx%dx%d points of 3 floats need %d."
                         % (a.mesh_file, os.path.getsize(a.mesh_file), nx, ny, nz, expected))
    xi, yi = _nearest_grid_point(a.coords_file, a.lon, a.lat)
    layers = read_column(a.mesh_file, nx, ny, xi, yi, min(NUM_INTERVALS + 1, nz))
    if len(layers) < NUM_INTERVALS + 1:
        raise ValueError("The mesh has only %d layers, VsD5H needs %d." % (nz, NUM_INTERVALS + 1))

    weights = [0.5] + [1.0] * (NUM_INTERVALS - 1) + [0.5]
    vsd5h = NUM_INTERVALS / sum(w / v for w, v in zip(weights, layers))

    if a.netcdf:
        vs_of_depth, model_step = netcdf_profile(a.netcdf, a.lon, a.lat, a.units)
        source = "NetCDF model %s" % a.netcdf
    else:
        layer_depths = np.arange(NUM_INTERVALS + 1) * float(h)
        vs_of_depth = lambda z: float(np.interp(z, layer_depths, layers))  # noqa: E731
        model_step = float(h)
        source = "AWP mesh, no NetCDF model given"
    #AR: (2026-10-15) Vs30 averages the top 30 m. With a model sampled every model_step metres in
    #AR: depth, those 30 m are a straight line between two samples and miss what lies between,
    #AR: so Vs30, and with it the reference Vs30 of the low frequencies, is less precise. At UCR a
    #AR: NetCDF every 200 m gave Vs30 263.5 against 368.1 from UCVM queried every metre.
    #AR: Only a warning.
    if model_step > 30.0:
        print("site_vs_profile: WARNING the velocity model is sampled every %.0f m in depth at the "
              "top, coarser than 30 m. Vs30 and Vs%d are interpolated between samples and the "
              "reference Vs30 of the low-frequency site response is less precise."
              % (model_step, NUM_INTERVALS * h))

    vs30 = travel_time_average(np.arange(30) + 0.5, vs_of_depth)
    vs5h = travel_time_average(np.arange(NUM_INTERVALS * h) + 0.5, vs_of_depth)

    print("site_vs_profile: lon %f lat %f, mesh point (%d, %d), Vs30 and Vs%d from %s"
          % (a.lon, a.lat, xi, yi, NUM_INTERVALS * h, source))
    print("site_vs_profile: mesh Vs of layers 0 to %d: %s"
          % (NUM_INTERVALS, ", ".join("%.1f" % v for v in layers)))
    with open(a.outfile, "w") as f:
        f.write("Vs30 = %.1f\nVs%d = %.1f\nVsD%d = %.1f\n"
                % (vs30, NUM_INTERVALS * h, vs5h, NUM_INTERVALS * h, vsd5h))
        #AR: Extra line for the workflow's warning. retrieve_vs does not write it, and the
        #AR: readers of the three values skip it.
        f.write("ModelDepthStep = %.1f\n" % model_step)

if __name__ == "__main__":
    main()
