#!/usr/bin/env python3
#AR: New script (2026-07-16), part of the stochastic-codes extension's "generic" (non-UCVM)
#AR: velocity-info backend (see cybershake_workflows/steps/stochastic/base.py's
#AR: vs30_from_grid()). It looks up the Vs30 value at a given lon/lat in a grid file that
#AR: comes from outside, for example a USGS Vs30 product (https://earthquake.usgs.gov/data/vs30/,
#AR: usually a GMT .grd or NetCDF). The workflow only interpolates this grid, it never builds it.
#AR: I kept it separate from UCVM on purpose. UCVM stays as it is for California and other SCEC
#AR: regions (retrieve_vs), and this script is the alternative for any other region.
#AR:
#AR: Output mirrors retrieve_vs.c's own text format, but with only the one line this script can
#AR: actually produce from a flat Vs30 grid (no depth information, so no Vs5H/VsD5H here --
#AR: those come from awp_mesh_vsx.py instead, sampling AWP's own velocity mesh):
#AR:   Vs30 = <value>
"""
Usage: vs30_from_grid.py <grid_file> <lon> <lat> <outfile>

<grid_file>: a NetCDF/GMT .grd Vs30 raster (opened via xarray).
<lon>, <lat>: site coordinates (decimal degrees).
<outfile>: output text file, one line "Vs30 = <value>".
"""
import sys
import xarray as xr

#AR: Common coordinate/variable name aliases across different Vs30 grid products --
#AR: tried in order until one matches what's actually in the file.
LON_NAMES = ("lon", "longitude", "x")
LAT_NAMES = ("lat", "latitude", "y")
VS30_VAR_NAMES = ("vs30", "Vs30", "VS30", "z")


def _first_present(candidates, available):
    for name in candidates:
        if name in available:
            return name
    return None


def main():
    if len(sys.argv) != 5:
        sys.stderr.write("Usage: %s <grid_file> <lon> <lat> <outfile>\n" % sys.argv[0])
        sys.exit(1)

    grid_file, lon_str, lat_str, outfile = sys.argv[1:5]
    lon, lat = float(lon_str), float(lat_str)

    ds = xr.open_dataset(grid_file)

    lon_name = _first_present(LON_NAMES, ds.coords) or _first_present(LON_NAMES, ds.dims)
    lat_name = _first_present(LAT_NAMES, ds.coords) or _first_present(LAT_NAMES, ds.dims)
    if lon_name is None or lat_name is None:
        raise ValueError(
            "Could not find longitude/latitude coordinates in %s (looked for %s / %s, "
            "found coords: %s)" % (grid_file, LON_NAMES, LAT_NAMES, list(ds.coords)))

    var_name = _first_present(VS30_VAR_NAMES, ds.data_vars)
    if var_name is None:
        data_vars = list(ds.data_vars)
        if len(data_vars) == 1:
            var_name = data_vars[0]
        else:
            raise ValueError(
                "Could not identify the Vs30 variable in %s (looked for %s, "
                "found data variables: %s)" % (grid_file, VS30_VAR_NAMES, data_vars))

    point = ds[var_name].interp({lon_name: lon, lat_name: lat}, method="linear")
    vs30 = float(point.values)

    with open(outfile, "w") as f:
        f.write("Vs30 = %.1f\n" % vs30)


if __name__ == "__main__":
    main()
