function wtBuildPitchActuator(sys)
%WTBUILDPITCHACTUATOR  Build the collective pitch actuator subsystem.
%
%   BOM reference: pitch bearing 1300, pitch drive 1310, backup store 1320.
%
%   PORTS
%     In   1  beta_cmd  [rad]     commanded pitch angle
%     Out  1  beta      [rad]     actual pitch angle
%          2  beta_rate [rad/s]   pitch rate (for load monitoring)
%
%   MODEL
%     Position saturation  -> rate limit -> first-order lag
%
%     tau * beta_dot + beta = beta_cmd_limited
%
%   THE RATE LIMIT IS THE IMPORTANT PART
%   ------------------------------------
%   BOM item 1310 gives the servo drives a finite slew capability (8 deg/s
%   here). Omitting that limit is the single most common way a wind turbine
%   model overstates gust rejection: with unlimited pitch rate the
%   controller appears to hold rated power through gusts that would in fact
%   produce a real overspeed excursion. The limit belongs in the plant, not
%   the controller, because it is a property of the hardware.
%
%   This models COLLECTIVE pitch -- one angle for all three blades. The BOM
%   describes three independent per-blade systems (1300/1310/1320 at Qty 3),
%   which additionally permits individual pitch control (IPC) to attenuate
%   asymmetric rotor loads. IPC needs a rotating-frame transform and per-blade
%   load feedback from BOM item 7280, and is not built here.
%
%   See also WTBUILDCONTROLLER, WTBUILDMODEL.

wtAdd('simulink/Sources/In1', [sys '/beta_cmd'], [30 100], 'Port','1');

% Position limits first: never command past feather or below fine pitch.
wtAdd('simulink/Discontinuities/Saturation', [sys '/posLimit'], [140 100], ...
    'UpperLimit','P.pitch.betaMax', ...
    'LowerLimit','P.pitch.betaMin');

% Hardware slew limit (BOM 1310).
wtAdd('simulink/Discontinuities/Rate Limiter', [sys '/rateLimit'], [250 100], ...
    'RisingSlewLimit','P.pitch.rateLimit', ...
    'FallingSlewLimit','-P.pitch.rateLimit');

% Actuator dynamics: first order.
wtAdd('simulink/Continuous/Transfer Fcn', [sys '/actuatorLag'], [360 100], ...
    'Numerator','[1]', ...
    'Denominator','[P.pitch.tau 1]');

% Pitch rate, for load reporting.
wtAdd('simulink/Continuous/Derivative', [sys '/dBeta'], [480 200]);

wtAdd('simulink/Sinks/Out1', [sys '/beta_out'],      [500 100], 'Port','1');
wtAdd('simulink/Sinks/Out1', [sys '/beta_rate_out'], [600 200], 'Port','2');

wtLine(sys, { ...
    'beta_cmd/1',    'posLimit/1'
    'posLimit/1',    'rateLimit/1'
    'rateLimit/1',   'actuatorLag/1'
    'actuatorLag/1', 'beta_out/1'
    'actuatorLag/1', 'dBeta/1'
    'dBeta/1',       'beta_rate_out/1'});

end
