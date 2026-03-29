#!/bin/bash
set -euo pipefail
TARGET="deploy@10.1.0.244"
SSH="ssh -o StrictHostKeyChecking=no"
# Build (merc target only, skip integration tests)
cd ackmud/src
make merc
cd -
# Deploy binary
scp -o StrictHostKeyChecking=no ackmud/src/merc $TARGET:/opt/mud/src/ackmud/src/merc
# Restart service
$SSH $TARGET "sudo systemctl restart mud"
echo "Deployed to ackmud41"
