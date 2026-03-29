#!/bin/bash
set -euo pipefail
TARGET="deploy@10.1.0.244"
SSH="ssh -o StrictHostKeyChecking=no"
# Build
cd ackmud/src
make merc
cd -
# Stop service, deploy binary, restart
$SSH $TARGET "sudo systemctl stop mud"
scp -o StrictHostKeyChecking=no ackmud/src/merc $TARGET:/opt/mud/src/ackmud/src/merc
$SSH $TARGET "sudo systemctl start mud"
echo "Deployed to ackmud41"
