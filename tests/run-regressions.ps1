$ErrorActionPreference = 'Stop'
python scripts/run_tests.py @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
