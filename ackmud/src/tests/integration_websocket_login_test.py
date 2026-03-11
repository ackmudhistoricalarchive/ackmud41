#!/usr/bin/env python3
import sys

from integration_websocket_test import main


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"integration_websocket_login_test failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
