function wtBuildDrivetrain(sys)
%WTBUILDDRIVETRAIN  Build the two-mass flexible drivetrain subsystem.
%
%   BOM reference: main shaft 2100, main bearings 2200, gearbox 2300,
%   coupling 2400, generator rotor inertia from 2600.
%
%   PORTS
%     In   1  T_aero    [Nm]     aerodynamic torque, low-speed shaft
%          2  T_gen     [Nm]     generator reaction torque, high-speed shaft
%     Out  1  omega_r   [rad/s]  rotor speed
%          2  omega_g   [rad/s]  generator speed
%          3  T_shaft   [Nm]     low-speed shaft torque
%          4  theta_d   [rad]    shaft torsional deflection
%
%   WHY TWO MASSES AND NOT ONE
%   --------------------------
%   A single lumped inertia is simpler and is adequate for energy-capture
%   studies, but it cannot represent the drivetrain torsional mode -- and
%   that mode is exactly what the torque controller can excite. The BOM's
%   own gearbox entry (2300) notes case-carburised gearing sized against
%   tooth-root fatigue; torsional oscillation is a direct contributor to
%   that load spectrum. A one-mass model would hide it entirely.
%
%   EQUATIONS
%     theta_d_dot = omega_r - omega_g / N
%     T_shaft     = K_shaft*theta_d + B_shaft*(omega_r - omega_g/N)
%     J_r*omega_r_dot = T_aero - T_shaft
%     J_g*omega_g_dot = T_shaft*eta/N - T_gen
%
%   Gearbox efficiency is applied to the torque transmitted to the
%   high-speed side, not to the reaction on the low-speed side, so the loss
%   appears where it physically occurs.
%
%   See also WTBUILDMODEL, WTPARAMETERS.

%% Inputs ------------------------------------------------------------------
wtAdd('simulink/Sources/In1', [sys '/T_aero_in'], [30 80],  'Port','1');
wtAdd('simulink/Sources/In1', [sys '/T_gen_in'],  [30 520], 'Port','2');

%% Rotor inertia -----------------------------------------------------------
wtAdd('simulink/Math Operations/Sum', [sys '/SumRotor'], [160 100], ...
    'Inputs','+-');

wtAdd('simulink/Math Operations/Gain', [sys '/invJr'], [250 100], ...
    'Gain','1/P.J_rotor');

wtAdd('simulink/Continuous/Integrator', [sys '/omega_r_int'], [340 100], ...
    'InitialCondition','P.omega_rInit');

%% Generator inertia -------------------------------------------------------
wtAdd('simulink/Math Operations/Sum', [sys '/SumGen'], [160 480], ...
    'Inputs','+-');

wtAdd('simulink/Math Operations/Gain', [sys '/invJg'], [250 480], ...
    'Gain','1/P.J_gen');

wtAdd('simulink/Continuous/Integrator', [sys '/omega_g_int'], [340 480], ...
    'InitialCondition','P.omega_rInit*P.N_gear');

%% Shaft torsion -----------------------------------------------------------
% omega_g referred to the low-speed side.
wtAdd('simulink/Math Operations/Gain', [sys '/omega_g_lss'], [460 300], ...
    'Gain','1/P.N_gear');

wtAdd('simulink/Math Operations/Sum', [sys '/SumTwistRate'], [560 260], ...
    'Inputs','+-');

wtAdd('simulink/Continuous/Integrator', [sys '/theta_d_int'], [650 220], ...
    'InitialCondition','0');

wtAdd('simulink/Math Operations/Gain', [sys '/K_shaft'], [750 220], ...
    'Gain','P.K_shaft');

wtAdd('simulink/Math Operations/Gain', [sys '/B_shaft'], [750 300], ...
    'Gain','P.B_shaft');

wtAdd('simulink/Math Operations/Sum', [sys '/SumShaftTorque'], [850 250], ...
    'Inputs','++');

%% Gearbox: torque to the high-speed side ---------------------------------
wtAdd('simulink/Math Operations/Gain', [sys '/gearRatio'], [120 420], ...
    'Gain','P.eta_gear/P.N_gear');

%% Outputs -----------------------------------------------------------------
wtAdd('simulink/Sinks/Out1', [sys '/omega_r_out'], [460 100], 'Port','1');
wtAdd('simulink/Sinks/Out1', [sys '/omega_g_out'], [460 480], 'Port','2');
wtAdd('simulink/Sinks/Out1', [sys '/T_shaft_out'], [950 250], 'Port','3');
wtAdd('simulink/Sinks/Out1', [sys '/theta_d_out'], [750 160], 'Port','4');

%% Signal routing ----------------------------------------------------------
wtLine(sys, { ...
    % --- rotor side ---
    'T_aero_in/1',     'SumRotor/1'
    'SumShaftTorque/1','SumRotor/2'
    'SumRotor/1',      'invJr/1'
    'invJr/1',         'omega_r_int/1'
    'omega_r_int/1',   'omega_r_out/1'

    % --- twist rate: omega_r - omega_g/N ---
    'omega_r_int/1',   'SumTwistRate/1'
    'omega_g_int/1',   'omega_g_lss/1'
    'omega_g_lss/1',   'SumTwistRate/2'

    % --- shaft torque: K*theta + B*dtheta ---
    'SumTwistRate/1',  'theta_d_int/1'
    'theta_d_int/1',   'K_shaft/1'
    'SumTwistRate/1',  'B_shaft/1'
    'K_shaft/1',       'SumShaftTorque/1'
    'B_shaft/1',       'SumShaftTorque/2'
    'SumShaftTorque/1','T_shaft_out/1'
    'theta_d_int/1',   'theta_d_out/1'

    % --- generator side ---
    'SumShaftTorque/1','gearRatio/1'
    'gearRatio/1',     'SumGen/1'
    'T_gen_in/1',      'SumGen/2'
    'SumGen/1',        'invJg/1'
    'invJg/1',         'omega_g_int/1'
    'omega_g_int/1',   'omega_g_out/1'});

end
