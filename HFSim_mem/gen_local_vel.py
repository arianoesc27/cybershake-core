#!/usr/bin/env python3

#AR: (2026-07-21) c0 and c1, the coefficients of the Qs-Vs relation qs=c0+c1*vs, were fixed in
#AR: the code with no way to change them. I checked in BBP's GP driver (bbp/comps/hfsims.py)
#AR: that 57 and 34 are GP's default (DEFAULT_C0 and DEFAULT_C1 in hfsims_cfg.py). BBP uses them
#AR: whenever the velmodel_config.txt of a region does not set CODEBASE_GP_C0/C1, and LA Basin
#AR: does not. They are now optional 3rd and 4th arguments, with 57 and 34 as defaults, so a
#AR: call with only 2 arguments does the same as before.

#AR: (2026-07-22) California has to keep running exactly as before by default (qs=c0+c1*vs,
#AR: byte identical). So I added an explicit `california` flag (5th argument, default 1)
#AR: instead of guessing from whether the input file has 6 columns. The LA Basin file already
#AR: HAS 6 columns (its own Qp and Qs, which were always ignored and recomputed), so guessing
#AR: from the column count would have changed California too. With california=0 the script uses
#AR: the Qs already in column 6 of the input file. Qp, column 5, is read by hb_high_ but never
#AR: used in any computation, only Qs matters, so both output columns get the same value.

import sys
import os

if len(sys.argv)<3:
	print("Usage: %s <1D velocity model> <output> [c0] [c1] [california]" % sys.argv[0])
	sys.exit(1)

velocity_model = sys.argv[1]
out_filename = sys.argv[2]
c0=float(sys.argv[3]) if len(sys.argv)>3 else 57
c1=float(sys.argv[4]) if len(sys.argv)>4 else 34
california=int(sys.argv[5]) if len(sys.argv)>5 else 1
i=0
vm_fp = open(velocity_model, "r")
vel_in_data = vm_fp.readlines()
vm_fp.close()
#vel_out = "local_%s" % velocity_model
#vel_out_fp = open(vel_out, "w")
vel_out_fp = open(out_filename, "w")
for line in vel_in_data:
    i += 1
    if line.startswith("#") or line.startswith("%"):
            continue
    pieces = line.split()
    if len(pieces)>=4:
        th=float(pieces[0])
        vp=float(pieces[1])
        vs=float(pieces[2])
        dn=float(pieces[3])
        if california:
            qs=c0+c1*vs
        else:
            if len(pieces)<6:
                print("Error: california=0 requires the input file's line %d to already provide Qp/Qs (6 columns), only found %d." % (i, len(pieces)))
                sys.exit(2)
            qs=float(pieces[5])
        if i==len(vel_in_data):
            th=0.0
        vel_out_fp.write("%8.4f %8.4f %8.4f %8.4f %8.2f %8.2f\n" % (th, vp, vs, dn, qs, qs))
    else:
        vel_out_fp.write(line)
vel_out_fp.flush()
vel_out_fp.close()
sys.exit(0)
