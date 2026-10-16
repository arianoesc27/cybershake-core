#!/bin/bash
# AR: BSC-MN5 adaptation of insert_dir.sh
# Uses our own cybercommands_SC_sqlite.jar (Hibernate5 + SQLite, built from this repo's
# CyberCommands source against lib/hibernate5); no longer depends on the MN4-restored copy.
# -c is required for -r (RotD) mode. DirectSynth's rotd.c writes RotD100/RotD50
# in units of g (see calc_acc, which divides by 980.665), and the insertion sanity check
# (abort if value < 0.005) is calibrated in cm/sec^2, so omitting -c can wrongly abort on
# legitimate low-g values or (if ever fixed to not require -c) silently store g-valued data.

set -o errexit

if [ $# -lt 1 ]; then
    echo "Usage: insert_dir.bsc-mn5.sh -c -r -p <post-processing-dir> -run <run_id> -server sqlite:<db_path> -periods <p1,p2,...>"
    exit 1
fi

#AR: Self-contained module load (2026-07-03) so this script works regardless of the
#AR: caller's environment, for example a COMPSs worker. I don't count on java already being
#AR: on PATH. `module load` is idempotent, safe to call even if already loaded.
source /etc/profile.d/01-module.sh 2>/dev/null || true
module load java-openjdk/11.0.18+10 2>/dev/null || true

CC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="$CC_DIR/lib"
CP="$CC_DIR/cybercommands_SC_sqlite.jar:$(ls $LIB/hibernate5/*.jar | tr '\n' ':')$LIB/sqlite-jdbc-3.27.2.1.jar:$LIB/sqlite-dialect-0.1.0.jar:$LIB/mysql-connector-java-5.0.6-bin.jar:$LIB/hibernate3/commons-cli-1.0.jar"

java -classpath "$CP" commands.CyberLoadamps $@
