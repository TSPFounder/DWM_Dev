# Bill of Materials — Utility-Scale Wind Turbine
### Geometric features and material selection by component

---

## 0. Design point and how to read this document

"Large wind turbine" spans roughly 1.5 MW to 15 MW, and every dimension below scales with
that choice. This BOM is written against one concrete, representative machine so the
numbers are self-consistent rather than generic:

| Parameter | Value | Note |
| --- | --- | --- |
| Rated electrical power | 3.0 MW | Mainstream modern onshore class |
| Configuration | 3-blade, horizontal-axis, **upwind** | Upwind avoids tower-shadow fatigue loading |
| Rotor diameter | 120 m | Swept area 11,310 m² |
| Hub height | 90 m | Tubular steel tower |
| Speed regulation | Variable-speed, full-span **variable pitch** | Pitch-to-feather for power limiting and braking |
| Drivetrain | 3-stage gearbox + DFIG | Direct-drive alternative noted in §2 |
| Rotor speed | 5.5 – 14.0 rpm | Tip speed ~75 m/s at rated |
| Cut-in / rated / cut-out | 3 / 11.5 / 25 m/s | |
| Survival wind speed | 59.5 m/s (3 s gust) | |
| Design class | IEC 61400-1 Class IIA | Medium wind, high turbulence |
| Design life | 20 years (25 with life extension) | ~10⁸ – 10⁹ fatigue cycles |
| Total mass (excl. foundation) | ≈ 420 t | Rotor ~65 t, nacelle ~130 t, tower ~225 t |

**Scaling note.** Blade mass scales roughly with R^2.3 and rotor torque with R³, so these
figures do not transfer to a 6 MW or 15 MW machine by simple ratio. Where a dimension is
load-driven rather than convention-driven, that is called out.

**Three constraints drive nearly every material choice below**, and are worth stating once
rather than repeating in each row:

1. **Fatigue, not ultimate strength, governs.** A turbine sees ~10⁸–10⁹ load cycles. Most
   components are sized by fatigue endurance and stiffness, and are nowhere near yield at
   rated load. This is why ductile iron beats stronger-but-notch-sensitive steel for
   castings, and why weld detail class often matters more than base-metal grade.
2. **Low-temperature toughness is mandatory.** Nacelle and rotor castings must retain
   impact toughness at −20 °C to −40 °C, hence the "-LT" grades throughout.
3. **Repair access is effectively nil.** A hub-internal component 90 m up costs a crane
   mobilisation to replace. Design margin is bought cheaply at build; it is extremely
   expensive later.

---

## 1000 — ROTOR ASSEMBLY

Total ≈ 65 t. This is the aerodynamic subsystem: it converts wind kinetic energy to shaft
torque and, via pitch, is the primary means of both power control and aerodynamic braking.

### 1100 — Blades (qty 3)

**Geometric features**

| Feature | Value / description |
| --- | --- |
| Length | 58.5 m (hub radius 1.5 m) |
| Planform | Tapered, non-linear chord distribution |
| Max chord | ≈ 3.9 m at 20–25 % span |
| Root | Circular, Ø 2.6 m — transitions to airfoil by ~15 % span |
| Tip chord | ≈ 0.6 m |
| Airfoil family | Thick sections inboard (DU 00-W-401, t/c ≈ 40 %) → thin outboard (NACA 64-618, t/c ≈ 18 %) |
| Aerodynamic twist | ≈ 13° total, washout from root to tip |
| Prebend | 3.0 m out-of-plane, toward upwind |
| Structural form | Two shear webs + load-bearing spar caps; closed aeroshell bonded along leading/trailing edge |
| Mass | ≈ 13.5 t each |

The **prebend** and the rotor's 5° shaft tilt and 3° precone exist for one reason: tower
strike clearance. Under extreme thrust the blade deflects downwind several metres, and
these three geometric offsets buy that clearance without the mass penalty of simply making
the blade stiffer. Blade stiffness is frequently governed by tip-to-tower clearance rather
than by stress.

**Materials**

| Element | Material | Rationale |
| --- | --- | --- |
| Spar caps | Unidirectional **glass/epoxy** (or **carbon/epoxy** pultruded planks) | Carries flapwise bending. Carbon raises stiffness-to-weight ~2× and is what makes >70 m blades viable; glass is cheaper and far more damage-tolerant. At 58.5 m either is defensible — carbon becomes near-mandatory beyond ~65 m |
| Shear webs | Biaxial glass/epoxy skins over foam core | Carries shear between spar caps |
| Aeroshell skins | Triaxial **E-glass/epoxy** laminate | Aerodynamic surface, torsional stiffness |
| Sandwich core | **PET or PVC structural foam**; balsa in high-compression zones | Panel buckling resistance at low mass. Balsa has better compressive strength, PET better moisture and recyclability behaviour |
| Resin system | **Epoxy**, vacuum infusion | Superior fatigue and bond strength vs. polyester; infusion gives high fibre volume fraction and low voids |
| Root inserts | Steel **T-bolt bushings** or bonded studs, 42CrMo4 | Transfers laminate load into discrete bolted joint |
| Leading edge protection | Polyurethane tape or elastomeric coating | Rain erosion at 75 m/s tip speed is a primary O&M cost |
| Surface | Polyurethane **gelcoat**, UV-stabilised, light grey | UV and moisture barrier |
| Lightning system | Copper receptors at tip and along span; ≥50 mm² copper down-conductor | Blades are the strike attachment point (IEC 61400-24) |

### 1200 — Hub

| Feature | Detail |
| --- | --- |
| Form | Near-spherical/tri-lobed hollow casting, Ø ≈ 3.4 m envelope |
| Blade flanges | 3 machined faces at 120°, Ø 2.6 m bolt circle |
| Wall thickness | 90–140 mm, thickened at flange transitions |
| Access | Internal cavity, man-accessible for pitch maintenance |
| Mass | ≈ 18 t |
| **Material** | **Ductile cast iron EN-GJS-400-18U-LT** (nodular/SG iron) |

Ductile iron rather than cast or fabricated steel: the hub is a geometrically complex,
thick-walled, highly-loaded casting where **notch insensitivity and damping matter more
than raw strength**. The `-LT` designation guarantees impact toughness at −20 °C; `18`
denotes ≥18 % elongation, which is what provides fatigue crack tolerance at the flange
fillets where stress concentrates.

### 1300 — Pitch System (3 sets, one per blade)

| Component | Geometry | Material |
| --- | --- | --- |
| Pitch bearing | Double-row four-point-contact ball bearing, Ø 2.6 m, integral gear teeth on inner race | Rings: **42CrMo4**, induction-hardened raceway (58–62 HRC), soft core. Balls: 100Cr6 |
| Pitch drive motor | Servo, ~5–12 kW | Standard electric machine construction |
| Pitch gearbox | Planetary, ratio ≈ 1:2000 | Housing ductile iron; gears **18CrNiMo7-6**, case-carburised |
| Pinion | Spur, meshes with bearing inner race | 18CrNiMo7-6, case-hardened |
| Backup power | Ultracapacitor bank or VRLA battery, per blade | Must feather the blade on total grid loss |

The backup store is a **safety-critical** item, not a convenience: with no grid and no
stored energy, the blades cannot be feathered and the rotor can overspeed to destruction.
Each blade's pitch system is independent, so any one of three can aerodynamically brake
the rotor alone.

### 1400 — Spinner / Nose Cone

| Feature | Detail |
| --- | --- |
| Form | Ogive/hemispherical fairing, Ø ≈ 3.6 m, with 3 blade cutouts |
| Function | Aerodynamic fairing and weather enclosure — **non-structural** |
| Material | **GFRP** (chopped strand / woven glass in polyester), internal steel support frame |
| Finish | Gelcoat, UV-stabilised |

### 1500 — Blade Root Bolting

| Feature | Detail |
| --- | --- |
| Quantity | 80–120 bolts per blade, M30–M36 |
| Preload | Hydraulically tensioned, ~70 % of proof load |
| Material | **Property class 10.9** alloy steel, hot-dip galvanised or zinc-flake coated |

Preload is what makes this joint survive: correctly tensioned, the bolts see only a small
fraction of the fluctuating blade load, and fatigue life becomes acceptable. An
under-tensioned bolt sees the full alternating load and fails in months. Bolt torque
audits are a scheduled maintenance item for exactly this reason.

---

## 2000 — DRIVETRAIN AND NACELLE

Total ≈ 130 t.

### 2100 — Main Shaft

| Feature | Detail |
| --- | --- |
| Form | Hollow forged shaft, OD 800 mm tapering to 600 mm, bore Ø 300 mm, length ≈ 2.6 m |
| Hub interface | Bolted flange, Ø 1.6 m |
| Gearbox interface | Shrink disc or integral flange |
| Bore purpose | Routes pitch system power and signal cabling to the hub |
| **Material** | **Forged 42CrMo4 (AISI 4140)**, quenched and tempered to ~280–320 HB |

Forged, not cast: the shaft carries fully-reversing bending superimposed on steady torque,
and forging aligns grain flow with the load path while eliminating casting porosity. Fillet
radii at every diameter change are generously sized and often rolled — this is a classic
fatigue-critical geometry where a sharp step is a crack initiation site.

### 2200 — Main Bearings

| Feature | Detail |
| --- | --- |
| Arrangement | Two-bearing (locating/floating), or single large moment bearing in 3-point layouts |
| Type | Spherical roller or **tapered roller**, bore Ø 800 mm |
| Housing | Ductile iron **EN-GJS-400-18U-LT** |
| Rings and rollers | **100Cr6** (SAE 52100) through-hardened, or 18CrNiMo7-6 case-carburised for larger sections |
| Cage | Machined brass or steel |
| Lubrication | Grease, automatic re-lubrication |
| Sealing | Labyrinth + contact lip seals |

Main bearings are a known reliability weak point. The load case is unusually hostile:
low speed (14 rpm — too slow to build a reliable hydrodynamic film), very high load, and
large fluctuating moments from asymmetric rotor loading, which together promote
micropitting and white-etching-crack failure modes.

### 2300 — Gearbox

| Feature | Detail |
| --- | --- |
| Configuration | 3 stages: 1 planetary (or 2) + 2 helical |
| Ratio | ≈ 1:104 (14 rpm → 1460 rpm) |
| Mass | ≈ 22 t |
| Housing | **EN-GJS-400-18U-LT** ductile iron, horizontally split |
| Gears | **18CrNiMo7-6**, case-carburised 1.5–2.5 mm, ground to ISO 1328 Class 5–6 |
| Planet bearings | Cylindrical roller, or journal bearings in newer designs |
| Shafts | 42CrMo4 or 34CrNiMo6, Q&T |
| Lubricant | Synthetic **PAO ISO VG 320**, forced circulation, filtered to 6 µm, with cooler and offline filtration |

Case-carburised gearing gives a hard, wear-resistant, compressively-stressed surface over a
tough core — the compressive residual stress in the case directly opposes the tensile
bending stress at the tooth root, which is where fatigue cracks start.

> **Direct-drive alternative.** Deleting the gearbox removes the single most
> failure-prone assembly and its entire lubrication system, at the cost of a much larger,
> heavier, permanent-magnet generator (Ø 5–6 m) with significant rare-earth content. Both
> topologies are current production practice; the trade is capital cost and nacelle mass
> against gearbox O&M risk and rare-earth supply exposure.

### 2400 — High-Speed Coupling and Brake

| Component | Geometry | Material |
| --- | --- | --- |
| Coupling | Flexible disc-pack or composite spacer, with **torque-limiting slip clutch** | Steel hubs; GFRP spacer tube for electrical isolation |
| Brake disc | Ø 1.0–1.2 m, 30–40 mm thick, on the high-speed shaft | Forged/machined carbon or low-alloy steel |
| Calipers | 2–4, hydraulically applied, **spring-applied / hydraulically released** | Cast steel body, sintered friction pads |

The brake is spring-applied and hydraulically *released* so that loss of hydraulic
pressure fails safe (brake on). It is a **parking and secondary brake only** — the primary
means of stopping the rotor is aerodynamic, by pitching the blades to feather. A mechanical
brake sized to stop a 3 MW rotor from rated speed unaided would be impractically large, and
applying one at speed imposes severe drivetrain torque. Electrical isolation in the coupling
prevents bearing currents from crossing between generator and gearbox.

### 2600 — Generator

| Feature | DFIG (baseline) | PMSG (direct-drive alternative) |
| --- | --- | --- |
| Speed | 1000–1800 rpm | 5.5–14 rpm |
| Voltage | 690 V | 690 V via full converter |
| Diameter | ≈ 1.1 m | ≈ 5.5 m |
| Mass | ≈ 9 t | ≈ 55 t |
| Cooling | Air (IC 611) or liquid | Liquid |

| Element | Material | Rationale |
| --- | --- | --- |
| Stator/rotor laminations | **Non-oriented electrical steel M270-50A**, 0.5 mm, insulated both faces | Thin insulated laminations suppress eddy-current loss |
| Windings | Copper, **Class F/H insulation** (155/180 °C) | Insulation class sets thermal derating |
| Permanent magnets (PMSG) | **NdFeB**, sintered, with Dy for elevated coercivity | Supply-chain and cost exposure; irreversible demagnetisation above ~150 °C |
| Frame | Fabricated **S355** steel or cast iron | |
| Slip rings (DFIG) | Copper alloy rings, graphite brushes | Wear item — a known DFIG maintenance burden |
| Bearings | Insulated or ceramic hybrid | Prevents electrical discharge machining of raceways from converter common-mode currents |

### 2700 — Bedplate / Mainframe

| Feature | Detail |
| --- | --- |
| Form | Two-part: cast front frame carrying main bearings, fabricated rear frame under generator |
| Length | ≈ 10 m overall |
| Mass | ≈ 20 t |
| Front (cast) | **EN-GJS-400-18U-LT** ductile iron — complex geometry, high concentrated bearing loads |
| Rear (fabricated) | **S355J2+N** plate, welded — simpler geometry, lower loads, cheaper per kg |

The mixed construction is deliberate: casting is used where geometric complexity and load
concentration justify tooling cost, fabrication where a simple welded structure suffices.

### 2800 — Nacelle Cover

| Feature | Detail |
| --- | --- |
| Dimensions | ≈ 12 m × 4 m × 4 m |
| Construction | GFRP sandwich panels on steel frame, bolted, with roof hatch and service crane opening |
| Material | **GFRP** with **fire-retardant resin** (per IEC 61400-24 / insurer requirement) |
| Features | Anemometry mast, aviation warning light, ventilation louvres, fall-arrest anchors |

Weather enclosure and acoustic attenuation only — carries no drivetrain load.

### 2900 — Cooling and Auxiliary Fluids

| System | Detail | Material |
| --- | --- | --- |
| Gearbox oil cooler | Oil/air or oil/water heat exchanger | Aluminium fin-and-tube |
| Generator cooling | Air-to-air heat exchanger or water/glycol loop | Aluminium / stainless |
| Converter cooling | Water/glycol | Stainless steel, EPDM hose |
| Hydraulic power unit | 200–250 bar, for brake and (if hydraulic) pitch | Steel reservoir, mineral hydraulic oil ISO VG 46 |

---

## 3000 — YAW SYSTEM

Keeps the rotor pointed into wind and manages cable twist.

| Component | Geometry | Material |
| --- | --- | --- |
| Yaw bearing | Ø ≈ 2.8 m ball or roller slewing ring, external gear teeth | Rings **42CrMo4**, induction-hardened raceway and teeth |
| Yaw drives | 4–8 units, electric motor + planetary gearbox + pinion | Gears 18CrNiMo7-6 case-carburised; ductile iron housings |
| Yaw brakes | 6–12 hydraulic calipers on a brake ring | Cast steel calipers, sintered pads |
| Yaw ring/friction pads | Sliding-bearing designs use PTFE-composite pads | UHMW-PE / PTFE composite |
| Yaw rate | ≈ 0.5 °/s | — |

Yaw is intentionally slow. Yawing a spinning rotor precesses it, and the resulting
**gyroscopic moment** on the main shaft and tower rises with yaw rate — so yaw speed is
limited by drivetrain load, not by the drives' capability. A permanent light brake torque
is also maintained during yaw to suppress backlash chatter in the drive train.

**Cable un-twist** is a functional requirement of this subsystem: the power and signal
cables hang down the tower in a deliberate loop, and the controller tracks cumulative yaw
angle and commands an untwist rotation at ±2–3 turns. Failure of this logic tears the
main power cable.

---

## 4000 — TOWER

≈ 225 t. Conical tubular steel, the near-universal onshore choice.

| Feature | Detail |
| --- | --- |
| Height | 87 m (hub height 90 m less nacelle centreline offset) |
| Sections | 3–4, flanged, road-transportable |
| Base diameter | 4.3 m — commonly limited by bridge/tunnel clearance, not by structure |
| Top diameter | 2.9 m |
| Wall thickness | 40 mm at base tapering to 12 mm at top |
| Flanges | Internal forged L-flanges, bolted |
| Flange bolts | 120–160 × M42–M48, **property class 10.9**, hydraulically tensioned |
| **Material** | **S355J2+N** or **S420** structural steel plate (EN 10025-2) |
| Corrosion protection | External: zinc-rich epoxy primer + polyurethane topcoat (ISO 12944 **C4/C5-M**). Internal: controlled dry environment |

**The tower is stiffness-driven, not strength-driven.** Its first bending natural frequency
must avoid both 1P (rotor rotation, 0.09–0.23 Hz) and 3P (blade passing, 0.28–0.70 Hz)
excitation. The standard "soft-stiff" design places the tower frequency between 1P and 3P —
a narrow window that usually sets wall thickness well above what stress alone would require.
This is why S355 remains standard: higher-strength steel would allow thinner walls, but
thinner walls move the frequency into a forbidden band and worsen shell buckling.

### 4100 — Tower Internals

| Item | Detail | Material |
| --- | --- | --- |
| Ladder | Full height, with fall-arrest rail | Galvanised steel / aluminium |
| Service lift | 2-person, 240 kg | Steel/aluminium car |
| Platforms | 4–5 intermediate + yaw deck | Galvanised steel grating |
| Power cables | 3-core + earth, ~630 mm² Cu or Al, with twist loop | XLPE-insulated, LSZH sheath |
| Fibre / control cabling | Ethernet, fibre optic | LSZH |
| Lighting, sockets, fire detection | — | — |
| Door | Reinforced opening at base — a **major stress concentration**, locally thickened | S355 |

---

## 5000 — FOUNDATION

Onshore gravity spread footing (site-dependent; piled or rock-anchored alternatives exist).

| Feature | Detail |
| --- | --- |
| Type | Octagonal or circular reinforced-concrete gravity base |
| Diameter | 18–20 m |
| Depth | 3.0–3.5 m at pedestal, 0.8–1.2 m at edge |
| Concrete volume | 700–900 m³ |
| Reinforcement | 60–80 t |
| Embedment | 2.5–3.5 m below grade, backfilled |
| Concrete | **C35/45**, sulphate-resisting cement where soil chemistry requires; low-heat mix for mass pours |
| Reinforcement steel | **B500B** ribbed bar |
| Anchor cage | 100–150 × M42–M64 high-strength bars, double nut, in two embedded ring flanges |
| Anchor bar material | High-strength alloy steel, grade 8.8/10.9 equivalent, corrosion-protected |
| Grout | Non-shrink cementitious, ≥80 MPa, under the base flange |

The foundation resists **overturning**, not primarily vertical load. Base diameter is set by
the overturning moment from rotor thrust acting at 90 m — mass and footprint provide the
restoring moment. Grout quality under the tower flange is critical and a known defect
source: a void there concentrates load into the anchor bolts and causes fatigue failure.

---

## 6000 — ELECTRICAL SYSTEM

| Component | Detail | Material notes |
| --- | --- | --- |
| Power converter | Back-to-back IGBT, partial-scale (~30 %) for DFIG, full-scale for PMSG | Cabinet: powder-coated steel; liquid-cooled heatsinks aluminium |
| Step-up transformer | 690 V → 33 kV, 3.3 MVA, in nacelle or tower base | **Cast-resin dry-type** (fire safety in an enclosed tower) or oil-filled with bund |
| MV switchgear | 33 kV ring main unit, vacuum or SF₆ | Steel enclosure |
| LV distribution | Auxiliary supplies for pitch, yaw, lighting, heaters | Steel cabinet, copper busbar |
| Slip ring assembly | Transfers power/signal into rotating hub | Copper alloy rings, graphite/silver brushes |
| Earthing | Ring electrode + radials, bonded to rebar and tower | Bare copper conductor ≥50 mm², copper-bonded rods |
| Lightning protection | Blade receptors → down-conductors → hub → shaft brush → tower → earth | Copper; spark gaps across bearings |
| UPS | Ride-through for controller and safety chain | — |

Bearings need a **dedicated lightning bypass** — brushes or spark gaps — because a strike
current allowed to pass through a bearing raceway pits it and destroys it. This is a
separate concern from the converter-induced bearing-current issue noted in §2600.

---

## 7000 — CONTROL SYSTEM

*Per the brief, controller **board-level** components are not decomposed and no materials
are selected for them. Enclosures, sensors, and actuators are covered.*

### 7100 — Main Controller

| Item | Detail |
| --- | --- |
| Hardware | Industrial PLC / dedicated turbine controller, DIN-rail mounted |
| Location | Tower-base cabinet, with a nacelle-top I/O node |
| Enclosure | **IP54 powder-coated sheet steel**, with heater and thermostat against condensation |
| Function | Supervisory state machine, torque and pitch control, yaw logic, fault handling, data logging |

**Control regions** the software implements — worth stating because the sensor list below
exists to serve them:

| Region | Wind speed | Strategy |
| --- | --- | --- |
| I | < 3 m/s | Idle / parked |
| II | 3 – 11.5 m/s | **Maximum power point tracking.** Pitch held at fine; generator torque commanded ∝ ω² to hold optimal tip-speed ratio |
| II½ | transition | Rotor speed limited (acoustic/structural) |
| III | 11.5 – 25 m/s | **Rated power limiting.** Torque held constant; pitch actively feathered to shed excess aerodynamic power |
| IV | > 25 m/s | Shutdown, feathered, yawed to minimise loads |

### 7200 — Sensors

| Sensor | Function | Location | Notes |
| --- | --- | --- | --- |
| Anemometer (×2, redundant) | Wind speed for supervisory logic | Nacelle roof mast | Heated in icing climates. Cup, ultrasonic, or ultrasonic+LiDAR |
| Wind vane (×2) | Yaw error | Nacelle roof mast | Redundancy prevents a single failed vane driving a persistent yaw misalignment — a significant silent AEP loss |
| Rotor speed encoder | Low-speed shaft position/speed | Main shaft | |
| Generator encoder | High-speed shaft speed | Generator | Torque control feedback |
| Pitch position encoders (×3) | Absolute blade angle | Each pitch bearing | Redundant, safety-rated |
| Yaw position encoder | Nacelle azimuth, cable twist count | Yaw ring | |
| Nacelle accelerometers | Tower fore-aft and side-side oscillation | Bedplate | Feeds active **tower damping** via torque/pitch modulation |
| Blade load sensors (optional) | Root bending moment | Blade root | **Fibre-optic strain gauges** or capacitive. Enables individual pitch control (IPC), reducing asymmetric fatigue loads |
| Temperature RTDs | Gearbox oil, bearings, generator windings, converter, ambient | Throughout | Pt100 |
| Oil pressure/level/particle count | Gearbox and hydraulic condition | Gearbox, HPU | Particle counting is the core of oil-debris condition monitoring |
| Vibration (CMS) | Drivetrain condition monitoring | Main bearing, gearbox, generator | Accelerometers, spectral analysis for incipient bearing faults |
| Ice detection | Blade ice accretion | Nacelle / blades | Triggers shutdown — ice throw is a safety hazard |
| Smoke/fire detection | Nacelle fire | Nacelle | |

### 7300 — Actuator Drives

| Item | Detail |
| --- | --- |
| Pitch drives (×3) | Servo drive per blade, independent, with backup energy store (see §1300) |
| Yaw drives | 4–8 VFD-controlled motors, torque-shared |
| Hydraulic solenoid valves | Brake apply/release |

### 7400 — Safety Chain

Hardwired, **independent of the main controller software**, per IEC 61400-1 and ISO 13849.
Any break in the chain triggers an emergency stop: blades to feather, brake applied.

| Element | Trigger |
| --- | --- |
| Overspeed detection | Independent speed sensor, hardware threshold |
| Vibration switch | Excessive nacelle motion |
| Emergency stop buttons | Nacelle, tower base, hub |
| Cable twist limit switch | Yaw over-rotation |
| Pitch system watchdog | Loss of pitch response |
| Controller watchdog | Main controller hang |

The defining property is **independence**: the safety chain must be able to stop the
turbine when the main controller is wrong, hung, or has itself caused the fault. A software
fault that could also disable the protection is not a protection system.

### 7500 — SCADA and Communications

| Item | Detail |
| --- | --- |
| Turbine-to-farm link | Fibre optic ring, IEC 61850 or vendor protocol |
| Farm controller | Curtailment, reactive power dispatch, grid-code compliance |
| Remote access | VPN, with logging and alarm annunciation |
| Data historian | 10-minute statistics + high-rate event capture for fault forensics |

---

## 8000 — AUXILIARY AND SAFETY

| Item | Detail | Material |
| --- | --- | --- |
| Service crane | 800 kg jib crane in nacelle | Steel |
| Fire suppression | Aerosol or water-mist in nacelle/converter | — |
| Aviation warning lights | Medium-intensity, nacelle roof | Per national CAA rules |
| Fall protection | Anchor points, rail systems, rescue kit | Galvanised steel |
| Ventilation/filtration | Nacelle air exchange with particulate filters | — |
| Dehumidifier | Tower interior corrosion control | — |
| Bird/bat deterrent (site-dependent) | Acoustic or curtailment-based | — |

---

## 9. Material summary and selection logic

| Material | Where used | Selected for |
| --- | --- | --- |
| **E-glass/epoxy, carbon/epoxy** | Blades | Specific stiffness, fatigue endurance, mouldability into aerodynamic form |
| **EN-GJS-400-18U-LT ductile iron** | Hub, bedplate front, gearbox and bearing housings | Castability of complex thick sections, notch insensitivity, vibration damping, −20 °C toughness |
| **42CrMo4 / 34CrNiMo6** | Main shaft, bearing rings, pitch/yaw rings | Through-hardenability in large sections, high fatigue strength, induction-hardenable raceways |
| **18CrNiMo7-6** | All gearing | Case-carburising gives hard wear surface + tough core + compressive residual stress at tooth root |
| **100Cr6 (52100)** | Rolling elements | Through-hardening bearing steel, high contact fatigue life |
| **S355J2+N / S420** | Tower, fabricated frames | Weldability, toughness, cost per unit stiffness — *stiffness* is what the tower actually needs |
| **Property class 10.9 bolting** | Blade root, tower flanges, foundation | High preload capacity; preload is what gives the joint fatigue life |
| **C35/45 concrete + B500B rebar** | Foundation | Mass and stiffness at lowest cost per unit restoring moment |
| **M270-50A electrical steel, copper, NdFeB** | Generator | Magnetic performance, conductivity, energy product |
| **GFRP (fire-retardant)** | Nacelle cover, spinner | Weather enclosure at low mass, corrosion-immune, non-structural |

---

## 10. Assumptions, exclusions, and where this needs a specialist

**Stated assumptions**
- 3.0 MW / 120 m rotor / 90 m hub, IEC Class IIA, onshore, geared DFIG drivetrain.
- Temperate climate. Cold-climate (−30 °C) or offshore variants change steel grades,
  coating specification, sealing, and add ice/marine provisions.
- Gravity spread foundation, assuming competent soil with bearing capacity ≥200 kPa.

**Deliberately excluded**
- Controller board-level electronics and their materials, per the brief.
- Fastener-by-fastener schedule, gasket/seal schedule, paint system layer detail.
- Part numbers, vendors, and costs.
- Offshore scope entirely (transition piece, monopile/jacket, scour protection, marine
  coatings, boat landing).

**Where these numbers are indicative rather than designed**

The dimensions here are representative of the class and internally consistent, but the
following are outputs of analysis that has not been performed and must not be taken as
design values:

- **Blade structural sizing** — requires aeroelastic simulation (BEM or CFD coupled with
  structural FE) across the full IEC design load case set. Tip-tower clearance under
  extreme gust is the binding constraint and is machine-specific.
- **Tower wall thickness schedule** — driven by the 1P/3P frequency separation and shell
  buckling check, both of which depend on the actual mass and stiffness distribution.
- **Foundation dimensions** — entirely site-specific, set by geotechnical investigation.
- **Fatigue life of every welded and bolted joint** — requires detail-category
  classification per Eurocode 3 / DNV-RP-C203 and a damage-equivalent load spectrum.

Certification (IEC 61400-1/-2/-24, DNV, TÜV) requires all of the above be demonstrated
by analysis and test. This BOM is a design-definition and material-selection reference,
not a certifiable design basis.
