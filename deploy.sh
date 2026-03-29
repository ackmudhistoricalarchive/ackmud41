#!/bin/bash
set -euo pipefail
TARGET="deploy@10.1.0.244"
SSH="ssh -o StrictHostKeyChecking=no"
# Build
cd ackmud/src \&\& make
# Deploy binary
scp -o StrictHostKeyChecking=no ackmud/src/merc $TARGET:/opt/mud/src/ackmud/src/merc
# Restart service
$SSH $TARGET "sudo systemctl restart mud"
echo "Deployed to ackmud41"
