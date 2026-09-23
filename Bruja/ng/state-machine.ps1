# Host dispatch. The socket work lives in state_machine.py.
$ErrorActionPreference = "Stop"
python "C:\Users\grego\Bruja\ng\state_machine.py"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
