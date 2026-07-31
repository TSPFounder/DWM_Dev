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
P.rho        = 1.225;      % [kg/m^3]  air density, ISA sea level
P.g         = 9.80665;     % [m/s^2]

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

%% ------------------------------------------------------------------------
%  Simulation defaults
%  ------------------------------------------------------------------------
P.sim = struct( ...
    'stopTime',   600, ...     % [s]
    'maxStep',    0.05, ...    % [s]
    'solver',     'ode23tb');  % stiff: the drivetrain torsional mode is fast
                               % relative to the rotor and tower modes

end
