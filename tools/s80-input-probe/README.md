# Series 80 input probe

Build with `TC`, `SDK`, and `OUT` as described by `build.sh`. Install the resulting
`InputProbe.exe` under `C:\System\Programs` in a private device data copy and run it
with `--run 'C:\System\Programs\InputProbe.exe'`.

This ARM client opens an ordinary window through WS32. It turns the background
yellow and prints pointer event type and guest coordinates on a button press.
It uses one SwissA font and does not depend on Eikon or Desk's pointer policy.
Negative asynchronous event/redraw completions terminate with a logged error.

For ROM wserv, enable `EKA2L1_ROM_WSERV=1`, leave `EKA2L1_ROM_FBS` unset, and install
the resident `tools/s80-sysstate` helper required by the controlled boot overlay.
