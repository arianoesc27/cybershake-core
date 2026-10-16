#!/bin/bash
# AR: BSC-MN5 adaptation of check_db.sh
# Uses our own patched CheckDBDataForSite.class (rupture-list filtering + SQLite
# column-alias fix); no longer depends on the MN4-restored db/ copy.

set -o errexit

#AR: Self-contained module load (2026-07-03) so this script works regardless of the
#AR: caller's environment, for example a COMPSs worker. I don't count on java already being
#AR: on PATH. `module load` is idempotent, safe to call even if already loaded.
source /etc/profile.d/01-module.sh 2>/dev/null || true
module load java-openjdk/11.0.18+10 2>/dev/null || true

DB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CYBERCOMMANDS_LIB='/gpfs/projects/bsc115/bsc203748/software/cybershake-core/CyberCommands/lib'
cd "$DB_DIR"
java -classpath ".:${CYBERCOMMANDS_LIB}/sqlite-jdbc-3.27.2.1.jar:mysql-connector-java-5.0.5-bin.jar:${CYBERCOMMANDS_LIB}/hibernate3/commons-cli-1.0.jar" CheckDBDataForSite $@
