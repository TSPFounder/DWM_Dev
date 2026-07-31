function wtBuildRotorAero(sys)
%WTBUILDROTORAERO  Build the rotor aerodynamics subsystem.
%
%   WTBUILDROTORAERO(SYS) populates the (already created) subsystem at path
%   SYS with the aerodynamic conversion from wind speed to shaft torque and
%   tower thrust.
%
%   BOM reference: assembly 1000 (Rotor Assembly) -- blades 1100, hub 1200.
%
%   PORTS
%     In   1  v_rel     [m/s]    wind speed RELATIVE to the nacelle
%          2  omega_r   [rad/s]  rotor speed
%          3  beta      [rad]    collective pitch angle
%     Out  1  T_aero    [Nm]     aerodynamic torque on the low-speed shaft
%          2  F_thrust  [N]      axial thrust on the rotor
%          3  Cp        [-]
%          4  lambda    [-]
%
%   EQUATIONS
%     lambda   = omega_r * R / v_rel
%     Cp       = Cp(lambda, beta)                    (2-D table, see WTCP)
%     Ct       = Ct(lambda, beta)
%     P_aero   = 0.5 * rho * A * Cp * v_rel^3
%     T_aero   = P_aero / omega_r
%     F_thrust = 0.5 * rho * A * Ct * v_rel^2
%
%   Note that v_rel, not the free-stream wind, drives every one of these.
%   The nacelle fore-aft velocity is subtracted upstream in the top-level
%   model, which is what produces aerodynamic damping of the tower mode. A
%   model that feeds free-stream wind straight in here will show a tower
%   oscillation with only ~1 % structural damping and will look far more
%   lightly damped than the real machine.
%
%   See also WTBUILDMODEL, WTGENERATEAEROTABLES.

%% Inputs ------------------------------------------------------------------
wtAdd('simulink/Sources/In1', [sys '/v_rel'],   [30 60],  'Port','1');
wtAdd('simulink/Sources/In1', [sys '/omega_r'], [30 180], 'Port','2');
wtAdd('simulink/Sources/In1', [sys '/beta'],    [30 300], 'Port','3');

%% Tip-speed ratio ---------------------------------------------------------
% Guard the divisor: at start-up and during shutdown v_rel passes through
% zero, and an unguarded 1/v generates Inf that propagates into the solver.
wtAdd('simulink/Discontinuities/Saturation', [sys '/vGuard'], [140 60], ...
    'UpperLimit','100', 'LowerLimit','0.25');

wtAdd('simulink/Math Operations/Gain', [sys '/R_gain'], [140 180], ...
    'Gain','P.R');

wtAdd('simulink/Math Operations/Product', [sys '/lambda'], [250 120], ...
    'Inputs','*/');

%% Aerodynamic coefficient tables -----------------------------------------
% Added through a wrapper because the library path for this block changed
% name between releases -- see WTADDLOOKUP2D.
wtAddLookup2D([sys '/CpTable'], [380 120], 'AT.Cp', 'AT.lambda', 'AT.beta');
wtAddLookup2D([sys '/CtTable'], [380 260], 'AT.Ct', 'AT.lambda', 'AT.beta');

%% Aerodynamic power and torque -------------------------------------------
wtAdd('simulink/Math Operations/Math Function', [sys '/v_cubed'], [250 400], ...
    'Operator','pow');
wtAdd('simulink/Sources/Constant', [sys '/three'], [140 440], 'Value','3');

wtAdd('simulink/Math Operations/Product', [sys '/P_aero_raw'], [500 380]);

wtAdd('simulink/Math Operations/Gain', [sys '/halfRhoA_P'], [600 380], ...
    'Gain','0.5*P.rho*P.A');

% T = P / omega, with the same divide-by-zero guard reasoning as above.
wtAdd('simulink/Discontinuities/Saturation', [sys '/omegaGuard'], [500 180], ...
    'UpperLimit','10', 'LowerLimit','0.05');

wtAdd('simulink/Math Operations/Product', [sys '/T_aero_calc'], [720 380], ...
    'Inputs','*/');

%% Thrust ------------------------------------------------------------------
wtAdd('simulink/Math Operations/Math Function', [sys '/v_squared'], [250 540], ...
    'Operator','square');

wtAdd('simulink/Math Operations/Product', [sys '/F_thrust_raw'], [500 520]);

wtAdd('simulink/Math Operations/Gain', [sys '/halfRhoA_T'], [620 520], ...
    'Gain','0.5*P.rho*P.A');

%% Outputs -----------------------------------------------------------------
wtAdd('simulink/Sinks/Out1', [sys '/T_aero'],   [850 380], 'Port','1');
wtAdd('simulink/Sinks/Out1', [sys '/F_thrust'], [760 520], 'Port','2');
wtAdd('simulink/Sinks/Out1', [sys '/Cp_out'],   [520 120], 'Port','3');
wtAdd('simulink/Sinks/Out1', [sys '/lambda_out'],[340 60],  'Port','4');

%% Signal routing ----------------------------------------------------------
wtLine(sys, { ...
    'v_rel/1',        'vGuard/1'
    'omega_r/1',      'R_gain/1'
    'R_gain/1',       'lambda/1'
    'vGuard/1',       'lambda/2'
    'lambda/1',       'CpTable/1'
    'beta/1',         'CpTable/2'
    'lambda/1',       'CtTable/1'
    'beta/1',         'CtTable/2'
    'vGuard/1',       'v_cubed/1'
    'three/1',        'v_cubed/2'
    'CpTable/1',      'P_aero_raw/1'
    'v_cubed/1',      'P_aero_raw/2'
    'P_aero_raw/1',   'halfRhoA_P/1'
    'omega_r/1',      'omegaGuard/1'
    'halfRhoA_P/1',   'T_aero_calc/1'
    'omegaGuard/1',   'T_aero_calc/2'
    'T_aero_calc/1',  'T_aero/1'
    'vGuard/1',       'v_squared/1'
    'CtTable/1',      'F_thrust_raw/1'
    'v_squared/1',    'F_thrust_raw/2'
    'F_thrust_raw/1', 'halfRhoA_T/1'
    'halfRhoA_T/1',   'F_thrust/1'
    'CpTable/1',      'Cp_out/1'
    'lambda/1',       'lambda_out/1'});

end
