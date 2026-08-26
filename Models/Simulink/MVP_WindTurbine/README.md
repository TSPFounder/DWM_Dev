# 3 MW Wind Turbine — Simulink model builder

MATLAB code that constructs the complete turbine model programmatically. Every
parameter traces to `Wind_Turbine_BOM.xlsx`.

## Quick start

```matlab
cd matlab
out = wtRunSimulation('ramp');    % builds the model, runs it, plots
```

That is the only file you need to run. It builds `wtTurbine3MW.slx`, simulates,
checks results, and plots.

Scenarios: `'ramp'` (default, sweeps both control regions), `'step'`,
`'turbulent'`, `'gust'` (IEC extreme operating gust).

## Files

| File | Role |
|---|---|
| `wtRunSimulation.m` | **Entry point.** Scenario, run, checks, plots |
| `wtParameters.m` | All parameters + derivation of dynamics from BOM masses |
| `wtCp.m` | Cp(λ,β) and Ct(λ,β) equations |
| `wtGenerateAeroTables.m` | Pre-computes the lookup surfaces |
| `wtBuildModel.m` | Top-level assembly and inter-subsystem wiring |
| `wtBuildRotorAero.m` | BOM 1000 — aerodynamic torque and thrust |
| `wtBuildDrivetrain.m` | BOM 2100–2600 — two-mass flexible drivetrain |
| `wtBuildPitchActuator.m` | BOM 1300–1320 — rate-limited pitch servo |
| `wtBuildGeneratorConverter.m` | BOM 2600, 6100 — torque source + losses |
| `wtBuildTowerDynamics.m` | BOM 4100–4130 — fore-aft modal model |
| `wtBuildYawSystem.m` | BOM 3100–3300 — deadband + rate-limited yaw |
| `wtBuildController.m` | BOM 7100–7250 — Region II torque, Region III pitch |
| `wtAdd.m`, `wtLine.m` | Small helpers for block placement and wiring |

## How the BOM drives the dynamics

Nothing dynamic is hardcoded. Each quantity is computed from a BOM mass or
dimension in `wtParameters.m`:

| Dynamic property | Derived from |
|---|---|
| `J_rotor` | BOM 1100 blade mass × length², with a mass-distribution coefficient calibrated against the NREL 5 MW reference |
| `J_gen` | BOM 2600 generator mass × rotating fraction × radius of gyration² |
| `K_shaft` | BOM 2100 shaft OD/ID/length, as a hollow circular section in torsion |
| `m_towerModal` | BOM top-head mass + ¼ tower mass (cantilever participation) |
| `K_tower` | Chosen to place f₁ inside the soft-stiff window between 1P and 3P |
| `K_opt` | ½ρπR⁵Cp_max / (λ_opt³N³) |
| `T_genRated` | Rated power / rated generator speed / efficiency |

Change a mass in the BOM, change it in `wtParameters.m`, and the model follows.

## Model structure

```
v_free ──×── cos(yawErr) ── v_eff ──(−)── ẋ_tower ──> v_rel
                                                        │
        ┌───────────────────────────────────────────────┘
        v
   [RotorAero] ──T_aero──> [Drivetrain] ──ω_g──> [Controller]
        │                        ^                     │
        │                        │                 T_gen_cmd
        └──F_thrust──> [Tower]   │                     v
                          │      └───T_gen──── [GenConverter]
                        ẋ_tower
        β <── [PitchActuator] <──β_cmd── [Controller]
```

Two feedback paths matter:

1. **ẋ_tower is subtracted from wind speed.** This aeroelastic coupling is what
   damps the tower mode — structural damping alone is only ~1%.
2. **β feeds back to the controller**, because the pitch gain schedule is a
   function of actual pitch angle.

No algebraic loops: every feedback passes through an integrator or a first-order lag.

## MATLAB version compatibility

Written to run on **R2011a and later**. The following were changed specifically
for that, and are worth knowing about if you port the code forward or back:

| Construct | Issue | What is used instead |
|---|---|---|
| `deg2rad` / `rad2deg` | Not in base MATLAB until R2015b | Explicit `*pi/180` |
| `yline` | R2018b | Local `wtHLine` helper using `plot` |
| `Simulink.BlockDiagram.arrangeSystem` | R2018b | Wrapped in `try`/`catch`; block positions are scripted explicitly anyway |
| `getSimulinkBlockHandle` | Post-R2011a | `get_param` inside `try`/`catch` |
| `.slx` model format | R2012a | Extension chosen via `verLessThan`; R2011a writes `.mdl` |
| `n-D Lookup Table` block path | Renamed after R2011a | `wtAddLookup2D` tries both names |
| `sim` returning `SimulationOutput` | Default changed | `'ReturnWorkspaceOutputs','on'` explicitly, with a base-workspace fallback |
| `rng` | R2011a | `try`/`catch` down to `randn('state',...)` |

**Remaining R2011a risk.** Simulink block *parameter* names occasionally changed
between releases and cannot be checked without running MATLAB. If a `set_param`
call fails, the message names the offending parameter — the fix is normally a
rename, not a redesign. The likeliest candidates are `ExtrapMethod` on the lookup
tables (already wrapped in `try`/`catch`) and the `Rate Limiter` slew-limit
parameter names.

## Verification status

**This code has not been executed.** No MATLAB or Octave was available in the
environment where it was written. What *was* checked, statically:

- every `wtLine` block reference resolves to a block some `wtAdd` creates
- top-level port references match each subsystem's actual port count
- Inport/Outport numbering is contiguous and unique per subsystem
- every Outport is driven; every Inport is used
- `function`/`end` and all delimiters balance

That covers the errors that typically break script-built Simulink models, but it
is not a substitute for running it. Expect to fix block-parameter names against
your Simulink release — those vary between versions and cannot be checked without
MATLAB. The `2-D Lookup Table` parameter names (`Table`,
`BreakpointsForDimension1/2`) are the most likely to need adjustment on older
releases.

## Limitations

A control-design and behavioural model, **not** a load-certification model:

- Cp/Ct is a generic empirical fit, not BEM output from the real blade geometry.
  Power capture is indicative; the curve shape is right, absolute numbers are not.
- Two structural DOF only (drivetrain torsion, tower fore-aft). No blade modes,
  no side-side, no torsion — blade loads cannot be extracted.
- Single-point wind. No rotor-plane sampling, shear, or tower shadow, so 1P/3P
  harmonics never appear. The model can verify the tower frequency separation
  exists; it cannot show the consequence of getting it wrong.
- Generator is a torque source. No grid model, so no fault ride-through or
  grid-code questions.
- Safety chain deliberately not modelled — its defining property is independence
  from controller software, which an in-model branch would misrepresent.

For IEC 61400-1 certification use an aeroelastic code (OpenFAST, Bladed, HAWC2)
driven by the full design load case set.
