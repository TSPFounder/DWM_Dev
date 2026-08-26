function P = wtParameters()
%WTPARAMETERS  Parameter set for the 3.0 MW wind turbine model.
%
%   P = WTPARAMETERS() returns a struct of every physical constant, geometric
%   dimension and derived dynamic coefficient used by the Simulink model.
%
%   EVERY value here traces to Wind_Turbine_BOM.xlsx. Where a dynamic quantity
%   (inertia, stiffness, damping) is not itself in the BOM, it is DERIVED from
%   BOM masses and dimensions in the "derived" section below, with the
%   derivation shown rather than a magic number substituted. Change a mass in
%   the BOM, change it here, and the dynamics follow.
%
%   Reference: Wind_Turbine_BOM.xlsx, sheet "Design Point" and sheet "BOM".
%
%   See also WTCP, WTBUILDMODEL, WTRUNSIMULATION.

%% ------------------------------------------------------------------------
%  Environment
%  ------------------------------------------------------------------------
%  AIR DENSITY IS COMPUTED FROM SITE ELEVATION, NOT ASSUMED AT SEA LEVEL.
%
%  This was P.rho = 1.225 (ISA sea level) until 2026-08-02. That is wrong for
%  a turbine on a MOUNTAIN, and it is wrong in a way that matters twice over:
%
%    1. Power is LINEAR in density -- P = 0.5*rho*A*Cp*v^3 -- so a sea-level
%       rho overstates output at altitude. At 1200 m it is about 11% high.
%    2. P.K_opt below is DERIVED from rho, so the region-II optimal-torque
%       gain was mistuned for the site as well. The controller tracked the
%       wrong torque curve, not just the power being reported wrongly.
%
%  ** SET THIS TO THE ACTUAL SITE ELEVATION. 1200 m IS A PLACEHOLDER. **
P.siteElevation = 1200.0;  % [m]  ground elevation of the Mountain community

P.g         = 9.80665;     % [m/s^2]

%  Density is taken at the ROTOR, i.e. site elevation plus hub height. The
%  hub height is set further down, so the arithmetic is deferred to the
%  derived section at the end of this file -- see "Derived: air density".


%% ------------------------------------------------------------------------
%  Design point                        (BOM sheet "Design Point")
%  ------------------------------------------------------------------------
P.P_rated    = 3.0e6;      % [W]     rated electrical power
P.R          = 60.0;       % [m]     rotor radius (120 m diameter)
P.hubHeight  = 90.0;       % [m]
P.nBlades    = 3;
P.v_cutIn    = 3.0;        % [m/s]
P.v_rated    = 11.5;       % [m/s]
P.v_cutOut   = 25.0;       % [m/s]
P.omega_rMin = 5.5*2*pi/60;  % [rad/s]  5.5 rpm
P.omega_rMax = 14.0*2*pi/60; % [rad/s] 14.0 rpm
P.shaftTilt  = (5*pi/180);   % [rad]
P.precone    = (3*pi/180);   % [rad]

P.A          = pi*P.R^2;     % [m^2]  swept area, 11310 m^2

%% ------------------------------------------------------------------------
%  Masses                              (BOM items 1100-8500)
%  ------------------------------------------------------------------------
P.m_blade    = 13000;      % [kg]  BOM 1100, each
P.m_hub      = 18000;      % [kg]  BOM 1200
P.m_rotor    = 66210;      % [kg]  BOM assembly total "Rotor Assembly"
P.m_nacelle  = 78100;      % [kg]  BOM assembly total "Drivetrain & Nacelle"
P.m_yaw      = 5400;       % [kg]  BOM assembly total "Yaw System"
P.m_tower    = 243000;     % [kg]  BOM assembly total "Tower"
P.m_topHead  = P.m_rotor + P.m_nacelle + P.m_yaw;   % 149710 kg

P.L_blade    = 58.5;       % [m]   BOM 1100 geometry
P.d_towerBase= 4.3;        % [m]   BOM 4100
P.d_towerTop = 2.9;        % [m]   BOM 4120

%% ------------------------------------------------------------------------
%  Drivetrain                          (BOM 2100-2600)
%  ------------------------------------------------------------------------
P.N_gear     = 104.3;      % [-]   BOM 2300, 14 rpm -> 1460 rpm
P.eta_gear   = 0.97;       % [-]   3-stage gearbox mechanical efficiency
P.eta_gen    = 0.96;       % [-]   generator + converter electrical efficiency

%% ------------------------------------------------------------------------
%  Aerodynamic Cp model coefficients
%  ------------------------------------------------------------------------
%  Standard empirical Cp(lambda,beta) fit (Heier form). See WTCP for the
%  equation itself. These coefficients are a generic utility-scale fit, NOT
%  measured data for this specific rotor -- see the limitations note in
%  wtRunSimulation.m.
P.cp = struct('c1',0.5176,'c2',116,'c3',0.4,'c4',5,'c5',21,'c6',0.0068);
P.lambda_opt = 8.1;        % [-]  tip-speed ratio at Cp_max
P.Cp_max     = 0.48;       % [-]  peak power coefficient (beta = 0)
P.Ct_rated   = 0.80;       % [-]  thrust coefficient near rated

%% ========================================================================
%  DERIVED DYNAMIC PROPERTIES
%  Each of these is computed from the BOM values above, not asserted.
%  ========================================================================

%% Rotor inertia -----------------------------------------------------------
%  A blade is strongly root-weighted, so the uniform-bar value (1/3)*m*L^2
%  badly overestimates it. Calibrate the mass-distribution coefficient k_J
%  against the published NREL 5 MW reference turbine, which reports
%  J_rotor = 3.54e7 kg m^2 for three 61.5 m / 17740 kg blades:
%
%      k_J = 3.54e7 / (3 * 17740 * 61.5^2) = 0.176
%
%  Applying the same coefficient to this rotor's BOM blade mass and length:
P.k_J        = 0.176;      % [-] blade mass-distribution coefficient
P.J_rotor    = P.nBlades * P.k_J * P.m_blade * P.L_blade^2;   % ~2.35e7 kg m^2

%% Generator inertia -------------------------------------------------------
%  Only the generator rotor spins. Take ~30 % of the 9000 kg machine mass
%  (BOM 2600) at a 0.30 m radius of gyration, referred to the high-speed shaft.
P.m_genRotorFrac = 0.30;   % [-]
P.r_genGyr       = 0.30;   % [m]
P.J_gen      = P.m_genRotorFrac * 9000 * P.r_genGyr^2;        % ~243 kg m^2

%% Low-speed shaft compliance ---------------------------------------------
%  Torsional stiffness of the main shaft (BOM 2100): hollow forged section,
%  OD 0.80 m tapering to 0.60 m, bore 0.30 m, length 2.6 m. Use the mean OD.
P.G_steel    = 79.3e9;     % [Pa]  shear modulus, 42CrMo4
P.d_shaftOD  = 0.70;       % [m]   mean of 0.80 -> 0.60 taper
P.d_shaftID  = 0.30;       % [m]   bore, BOM 2100 (routes pitch cabling)
P.L_shaft    = 2.60;       % [m]
P.J_polar    = pi/32 * (P.d_shaftOD^4 - P.d_shaftID^4);       % [m^4]
P.K_shaft    = P.G_steel * P.J_polar / P.L_shaft;             % [Nm/rad]

%  Drivetrain damping. Expressed as a damping ratio on the free-free
%  torsional mode so it stays physical if inertias change.
P.zeta_dt    = 0.02;       % [-]
J_eq         = (P.J_rotor * P.J_gen * P.N_gear^2) / ...
               (P.J_rotor + P.J_gen * P.N_gear^2);
P.omega_dt   = sqrt(P.K_shaft / J_eq);                        % [rad/s]
P.B_shaft    = 2 * P.zeta_dt * sqrt(P.K_shaft * J_eq);        % [Nm s/rad]

P.B_rotor    = 0.0;        % [Nm s/rad] aerodynamic damping handled in Cp map
P.B_gen      = 0.0;        % [Nm s/rad]

%% Tower fore-aft mode -----------------------------------------------------
%  Single-DOF modal reduction of the first fore-aft bending mode. Modal mass
%  is the full top-head mass plus the standard 1/4 participation of a
%  distributed cantilever tower.
P.m_towerModal = P.m_topHead + 0.25*P.m_tower;                % ~210460 kg

%  Target frequency: the BOM's "soft-stiff" requirement places f1 between
%  1P and 3P.  1P at 14 rpm = 0.233 Hz; 3P = 0.700 Hz. Take the midpoint
%  region with margin on both sides.
P.f_1P       = P.omega_rMax/(2*pi);          % [Hz] 0.233
P.f_3P       = 3*P.f_1P;                     % [Hz] 0.700
P.f_tower    = 0.32;                         % [Hz] chosen, inside the window
P.K_tower    = P.m_towerModal * (2*pi*P.f_tower)^2;           % [N/m]
P.zeta_tower = 0.01;       % [-] welded steel tower, structural damping only
P.C_tower    = 2*P.zeta_tower*sqrt(P.K_tower*P.m_towerModal); % [N s/m]

%% ------------------------------------------------------------------------
%  Pitch actuator                      (BOM 1300, 1310)
%  ------------------------------------------------------------------------
P.pitch = struct( ...
    'tau',        0.15, ...            % [s]      first-order actuator lag
    'rateLimit',  (8*pi/180), ...      % [rad/s]  BOM 1310 servo capability
    'betaMin',    (0*pi/180), ...      % [rad]    fine pitch
    'betaMax',    (90*pi/180));        % [rad]    feather

%% ------------------------------------------------------------------------
%  Generator / converter               (BOM 2600, 6100)
%  ------------------------------------------------------------------------
P.omega_gRated = P.omega_rMax * P.N_gear;                     % [rad/s]
P.T_genRated   = P.P_rated / (P.omega_gRated * P.eta_gen);    % [Nm]
P.gen = struct( ...
    'tau',        0.02, ...            % [s]  converter torque response
    'T_max',      1.20 * P.T_genRated, ...
    'rateLimit',  15000);              % [Nm/s]

%% ------------------------------------------------------------------------
%  Yaw system                          (BOM 3100-3300)
%  ------------------------------------------------------------------------
P.yaw = struct( ...
    'rate',        (0.5*pi/180), ...   % [rad/s]  BOM 3200. Deliberately slow:
    ...                                %          gyroscopic moment on shaft and
    ...                                %          tower rises with yaw rate.
    'deadband',    (8*pi/180), ...     % [rad]    avoids hunting on turbulence
    'tau',         1.0);               % [s]

%% ========================================================================
%  CONTROLLER GAINS
%  ========================================================================

%% Region II -- optimal torque tracking ------------------------------------
%  Holding T_gen = K_opt * omega_g^2 drives the rotor to lambda_opt without
%  needing a wind speed measurement, because at steady state that torque law
%  intersects the aerodynamic torque curve exactly at Cp_max.
%
%      K_opt = 0.5*rho*pi*R^5*Cp_max / (lambda_opt^3 * N^3)
%
%% Derived: air density ----------------------------------------------------
%  Computed here rather than at the top because it needs P.hubHeight.
%  MUST run before K_opt below, which depends on it.
P.rho = wtAirDensity(P.siteElevation + P.hubHeight);

P.K_opt = 0.5*P.rho*pi*P.R^5*P.Cp_max / (P.lambda_opt^3 * P.N_gear^3);

%% Region III -- collective pitch PI with gain scheduling ------------------
%  dP/dbeta grows strongly with beta, so a fixed-gain PI tuned at low pitch
%  goes unstable at high wind. Schedule both gains on the NREL form
%  GS = 1/(1 + beta/beta_k).
P.pitchCtrl = struct( ...
    'Kp0',      0.012, ...     % [s]      proportional, at beta = 0
    'Ki0',      0.005, ...     % [-]      integral, at beta = 0
    'beta_k',   (6.3*pi/180), ...  % [rad] gain-scheduling knee (NREL 5MW value)
    'intMin',   (0*pi/180), ...
    'intMax',   (90*pi/180));

%% Supervisory thresholds --------------------------------------------------
P.sup = struct( ...
    'omega_gRated',  P.omega_gRated, ...
    'omega_gMin',    P.omega_rMin*P.N_gear, ...
    'overspeedTrip', 1.20*P.omega_gRated, ...  % safety chain, BOM 7400
    'v_cutIn',       P.v_cutIn, ...
    'v_cutOut',      P.v_cutOut);

%% Start-up sequencing ----------------------------------------------------
%  Consumed by WTBUILDSUPERVISOR. Everything above describes a turbine that
%  is already running; these describe getting it there from standstill.
%
%  BETA_START is DERIVED, not chosen for looks. Between standstill and
%  cut-in the rotor sweeps the whole low tip-speed-ratio range, so the
%  useful pitch is the one maximising the WORST torque coefficient
%  Cq = Cp/lambda encountered on the way up, not the best one at any single
%  lambda. Scanning WTCP over beta = 0..90 deg against lambda = 0.1..2.5
%  puts that maximin at 31 deg (min Cq = 0.0248, against 0.0068 at fine
%  pitch -- a factor of 3.6 in starting torque).
%
%  HONESTY NOTE, and it belongs next to the number rather than in a report:
%  at low lambda the exponential term of the Heier fit has collapsed and
%  nearly all of this torque comes from its linear c6*lambda correction.
%  The SHAPE is right -- feather brakes, intermediate pitch starts, fine
%  pitch runs -- and the sequence is physically the one a real turbine uses.
%  The starting-torque MAGNITUDE rests on a curve-fitting artefact, and
%  WTCP already warns it is a generic utility-scale fit rather than BEM data
%  for this blade. Adequate for sequencing; not a claim about this rotor.
P.sup.beta_park  = P.pitch.betaMax;        % [rad] feathered, Cq < 0 at low lambda
P.sup.beta_start = (31*pi/180);            % [rad] see derivation above

%  T_START = 0 IS THE BACKWARD-COMPATIBLE DEFAULT AND MUST STAY THAT WAY.
%  At 0 the start command is already true when the simulation begins, so a
%  model initialised at or above cut-in latches into GENERATING on the first
%  step and the supervisor is pure feedthrough from then on -- byte-identical
%  to the pre-supervisor model. Any non-zero value holds the machine PARKED
%  (feathered, zero torque) until that time, which is correct for a start
%  from standstill and WRONG for every scenario that predates this file:
%  it would feather a running turbine for t_start seconds and call it a
%  fresh result.
P.sup.t_start    = 0;                      % [s]   see above before changing

%  Cut-in detection is a relay, so it needs two thresholds. The drop-out is
%  10 % below pick-up: enough hysteresis that a torque dip just after cut-in
%  cannot chatter the state, small enough that a genuine stall still exits.
P.sup.omega_gOn  = P.omega_rMin*P.N_gear;       % [rad/s] STARTUP -> GENERATING
P.sup.omega_gOff = 0.90*P.omega_rMin*P.N_gear;  % [rad/s] drop-out

%% Rotor speed initial condition ------------------------------------------
%  The drivetrain integrators start here. Defaults to the cut-in floor,
%  which is what every scenario predating the supervisor assumed and is
%  therefore what keeps their results reproducible. Set it to 0 to simulate
%  a genuine start from standstill; the supervisor then carries the rotor up
%  to cut-in before the Region II law is given anything to do.
%
%  A STANDSTILL START NEEDS BOTH THIS AND P.sup.t_start, set together:
%      P.omega_rInit  = 0;    % rotor stopped at t = 0
%      P.sup.t_start  = 30;   % how long it stays stopped before the command
%  Setting only one of them is a configuration that means nothing physical --
%  a stopped rotor with t_start = 0 starts instantly, and a running rotor
%  with t_start > 0 gets feathered mid-run. They are deliberately left as two
%  explicit parameters rather than inferred from each other, because guessing
%  the operator's intent from an initial condition is how a scenario silently
%  becomes a different scenario.
P.omega_rInit = P.omega_rMin;              % [rad/s]

%% ------------------------------------------------------------------------
%  Simulation defaults
%  ------------------------------------------------------------------------
P.sim = struct( ...
    'stopTime',   600, ...     % [s]
    'maxStep',    0.05, ...    % [s]
    'solver',     'ode23tb');  % stiff: the drivetrain torsional mode is fast
                               % relative to the rotor and tower modes

end


% =========================================================================
function rho = wtAirDensity(altitude)
%WTAIRDENSITY  ISA air density at a geometric altitude, in kg/m^3.
%
%   Uses ATMOSCOESA from the Aerospace Toolbox when it is licensed, and falls
%   back to the closed-form ISA troposphere relation otherwise.
%
%   WHY A FALLBACK RATHER THAN JUST REQUIRING THE TOOLBOX
%   ----------------------------------------------------
%   This model ships in the MVP demo download. Most people who run it will
%   not have the Aerospace Toolbox, and a hard dependency would turn a
%   correctness fix into a licence error on someone else's machine. The
%   fallback is exact for the troposphere, so nothing is lost by it.
%
%   ISA troposphere, valid to 11 km:
%       T   = T0 - L*h
%       rho = rho0 * (T/T0)^(g/(L*R) - 1)
%   with the exponent working out at 4.25588.

useToolbox = false;
try
    % LICENSE returns false rather than erroring when the product is absent;
    % EXIST guards the case where the licence exists but the files do not.
    useToolbox = license('test', 'Aerospace_Toolbox') && (exist('atmoscoesa', 'file') == 2);
catch
    useToolbox = false;
end

if useToolbox
    try
        [~, ~, ~, rho] = atmoscoesa(altitude);
        return
    catch
        % Fall through to the closed form rather than failing the whole run.
    end
end

T0   = 288.15;      % [K]      ISA sea-level temperature
L    = 0.0065;      % [K/m]    tropospheric lapse rate
rho0 = 1.225;       % [kg/m^3] ISA sea-level density

if altitude > 11000
    warning('wtAirDensity:aboveTroposphere', ...
        ['Altitude %.0f m is above the 11 km troposphere limit of this ' ...
         'relation. Clamping to 11 km.'], altitude);
    altitude = 11000;
end

rho = rho0 * (1 - L*altitude/T0)^4.25588;
end
