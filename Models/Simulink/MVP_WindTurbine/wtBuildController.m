function wtBuildController(sys)
%WTBUILDCONTROLLER  Build the turbine controller subsystem.
%
%   BOM reference: main turbine controller 7100, encoders 7230/7240/7250,
%   anemometry 7210, safety chain 7400.
%
%   PORTS
%     In   1  omega_g   [rad/s]  generator speed (BOM 7240)
%          2  v_wind    [m/s]    nacelle anemometer (BOM 7210)
%          3  beta      [rad]    measured pitch angle (BOM 7250)
%     Out  1  T_gen_cmd [Nm]     generator torque demand
%          2  beta_cmd  [rad]    collective pitch demand
%          3  region    [-]      2 or 3, for logging
%
%   The two loops implement the control regions tabulated on the BOM's
%   "Control Regions" sheet.
%
%   REGION II -- optimal torque tracking
%   ------------------------------------
%       T_gen = K_opt * omega_g^2
%
%   This drives the rotor to lambda_opt WITHOUT using the anemometer at all.
%   That matters: nacelle anemometry sits in the rotor wake and is not
%   accurate enough to close a fast loop on. The torque law works because at
%   steady state it intersects the aerodynamic torque curve exactly at
%   Cp_max, for any wind speed.
%
%   REGION III -- rated power limiting
%   ----------------------------------
%   Torque is held at rated and collective pitch regulates speed:
%
%       e   = omega_g - omega_g_rated
%       GS  = 1 / (1 + beta/beta_k)
%       beta_cmd = GS*Kp0*e + integral(GS*Ki0*e)
%
%   WHY THE GAIN SCHEDULE IS NOT OPTIONAL
%   -------------------------------------
%   The sensitivity of aerodynamic power to pitch, dP/dbeta, grows steeply
%   with beta. A PI tuned at fine pitch (where dP/dbeta is small, so gains
%   must be large) becomes drastically over-gained at 20 deg of pitch and
%   goes unstable. GS = 1/(1+beta/beta_k) is the standard NREL form and is
%   what makes one linear controller work across the whole above-rated range.
%
%   Region transition is handled implicitly rather than by a mode switch:
%   below rated speed the error is negative, the PI drives to its lower
%   limit, and pitch sits at fine. The torque law meanwhile saturates at
%   rated. No explicit state machine, and therefore no switching transient.
%
%   NOT INCLUDED
%   ------------
%   The safety chain (BOM 7400) is deliberately absent from this subsystem.
%   Per its BOM entry its defining property is INDEPENDENCE from controller
%   software -- it must stop the turbine when this controller is wrong, hung,
%   or has itself caused the fault. Modelling it as a branch inside the same
%   subsystem would misrepresent exactly the property that makes it a
%   protection system. It belongs in a separate model if it is modelled.
%
%   Also absent: start-up/shutdown sequencing, drivetrain damping injection,
%   tower damping via pitch, and individual pitch control.
%
%   See also WTBUILDMODEL, WTPARAMETERS.

%% Inputs ------------------------------------------------------------------
wtAdd('simulink/Sources/In1', [sys '/omega_g'], [30 120], 'Port','1');
wtAdd('simulink/Sources/In1', [sys '/v_wind'],  [30 640], 'Port','2');
wtAdd('simulink/Sources/In1', [sys '/beta_fb'], [30 460], 'Port','3');

%% ------------------------------------------------------------------------
%  TORQUE LOOP
%  ------------------------------------------------------------------------
wtAdd('simulink/Math Operations/Math Function', [sys '/omega_g_sq'], [160 120], ...
    'Operator','square');

wtAdd('simulink/Math Operations/Gain', [sys '/K_opt'], [270 120], ...
    'Gain','P.K_opt');

wtAdd('simulink/Sources/Constant', [sys '/T_rated'], [270 200], ...
    'Value','P.T_genRated');

% min(K_opt*w^2, T_rated): Region II law below rated, flat at rated above.
wtAdd('simulink/Math Operations/MinMax', [sys '/T_select'], [390 150], ...
    'Function','min', 'Inputs','2');

wtAdd('simulink/Sinks/Out1', [sys '/T_gen_cmd'], [520 150], 'Port','1');

%% ------------------------------------------------------------------------
%  PITCH LOOP
%  ------------------------------------------------------------------------
wtAdd('simulink/Sources/Constant', [sys '/omega_g_ref'], [160 340], ...
    'Value','P.sup.omega_gRated');

wtAdd('simulink/Math Operations/Sum', [sys '/SumSpeedErr'], [270 300], ...
    'Inputs','+-');

%% Gain schedule: GS = 1/(1 + beta/beta_k) --------------------------------
wtAdd('simulink/Math Operations/Gain', [sys '/invBetaK'], [160 460], ...
    'Gain','1/P.pitchCtrl.beta_k');

wtAdd('simulink/Sources/Constant', [sys '/one'], [160 540], 'Value','1');

wtAdd('simulink/Math Operations/Sum', [sys '/SumGS'], [270 480], ...
    'Inputs','++');

wtAdd('simulink/Math Operations/Math Function', [sys '/GS'], [370 480], ...
    'Operator','reciprocal');

%% Proportional branch -----------------------------------------------------
wtAdd('simulink/Math Operations/Product', [sys '/errGS_P'], [390 300]);

wtAdd('simulink/Math Operations/Gain', [sys '/Kp'], [490 300], ...
    'Gain','P.pitchCtrl.Kp0');

%% Integral branch ---------------------------------------------------------
wtAdd('simulink/Math Operations/Product', [sys '/errGS_I'], [390 380]);

wtAdd('simulink/Math Operations/Gain', [sys '/Ki'], [490 380], ...
    'Gain','P.pitchCtrl.Ki0');

% Output-limited integrator. The limits ARE the anti-windup: without them
% the integral state charges up through the whole below-rated region and the
% turbine overspeeds badly on the first gust that reaches rated.
wtAdd('simulink/Continuous/Integrator', [sys '/pitchInt'], [590 380], ...
    'InitialCondition','0', ...
    'LimitOutput','on', ...
    'UpperSaturationLimit','P.pitchCtrl.intMax', ...
    'LowerSaturationLimit','P.pitchCtrl.intMin');

%% PI sum and output limit -------------------------------------------------
wtAdd('simulink/Math Operations/Sum', [sys '/SumPI'], [700 330], ...
    'Inputs','++');

wtAdd('simulink/Discontinuities/Saturation', [sys '/betaLimit'], [790 330], ...
    'UpperLimit','P.pitch.betaMax', ...
    'LowerLimit','P.pitch.betaMin');

wtAdd('simulink/Sinks/Out1', [sys '/beta_cmd'], [890 330], 'Port','2');

%% ------------------------------------------------------------------------
%  REGION INDICATOR (logging only -- drives nothing)
%  ------------------------------------------------------------------------
wtAdd('simulink/Logic and Bit Operations/Relational Operator', ...
    [sys '/isRegion3'], [390 640], 'Operator','>=');

wtAdd('simulink/Sources/Constant', [sys '/vRatedConst'], [270 700], ...
    'Value','P.v_rated');

wtAdd('simulink/Math Operations/Gain', [sys '/regionScale'], [500 640], ...
    'Gain','1');

wtAdd('simulink/Sources/Constant', [sys '/two'], [500 720], 'Value','2');

wtAdd('simulink/Math Operations/Sum', [sys '/SumRegion'], [610 660], ...
    'Inputs','++');

wtAdd('simulink/Sinks/Out1', [sys '/region'], [710 660], 'Port','3');

%% Signal routing ----------------------------------------------------------
wtLine(sys, { ...
    % --- torque loop ---
    'omega_g/1',      'omega_g_sq/1'
    'omega_g_sq/1',   'K_opt/1'
    'K_opt/1',        'T_select/1'
    'T_rated/1',      'T_select/2'
    'T_select/1',     'T_gen_cmd/1'

    % --- speed error ---
    'omega_g/1',      'SumSpeedErr/1'
    'omega_g_ref/1',  'SumSpeedErr/2'

    % --- gain schedule ---
    'beta_fb/1',      'invBetaK/1'
    'one/1',          'SumGS/1'
    'invBetaK/1',     'SumGS/2'
    'SumGS/1',        'GS/1'

    % --- proportional ---
    'SumSpeedErr/1',  'errGS_P/1'
    'GS/1',           'errGS_P/2'
    'errGS_P/1',      'Kp/1'
    'Kp/1',           'SumPI/1'

    % --- integral ---
    'SumSpeedErr/1',  'errGS_I/1'
    'GS/1',           'errGS_I/2'
    'errGS_I/1',      'Ki/1'
    'Ki/1',           'pitchInt/1'
    'pitchInt/1',     'SumPI/2'

    % --- output ---
    'SumPI/1',        'betaLimit/1'
    'betaLimit/1',    'beta_cmd/1'

    % --- region indicator ---
    'v_wind/1',       'isRegion3/1'
    'vRatedConst/1',  'isRegion3/2'
    'isRegion3/1',    'regionScale/1'
    'regionScale/1',  'SumRegion/1'
    'two/1',          'SumRegion/2'
    'SumRegion/1',    'region/1'});

end
