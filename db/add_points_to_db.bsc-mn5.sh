#!/bin/bash
# AR: BSC-MN5 wrapper for AddPointsToDB, mirroring check_db.bsc-mn5.sh's exact pattern (same
# db/ directory, same classpath, same DBConnect.java it already depends on. The #AR comments
# in that file explain my fixes for the MySQL or SQLite driver and the connection handling).
# Usage: add_points_to_db.bsc-mn5.sh <ERF_ID> <flat directory of rupture .txt files> sqlite:<db path>

set -o errexit

#AR: Self-contained module load (2026-07-03 convention, same as check_db.bsc-mn5.sh) so this
#AR: works regardless of the caller's environment (e.g. a COMPSs worker).
source /etc/profile.d/01-module.sh 2>/dev/null || true
module load java-openjdk/11.0.18+10 2>/dev/null || true

DB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CYBERCOMMANDS_LIB='/gpfs/projects/bsc115/bsc203748/software/cybershake-core/CyberCommands/lib'
cd "$DB_DIR"
java -classpath ".:${CYBERCOMMANDS_LIB}/sqlite-jdbc-3.27.2.1.jar:mysql-connector-java-5.0.5-bin.jar:${CYBERCOMMANDS_LIB}/hibernate3/commons-cli-1.0.jar" AddPointsToDB $@
