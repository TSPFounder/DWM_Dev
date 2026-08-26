function wtBuildGeneratorConverter(sys)
%WTBUILDGENERATORCONVERTER  Build the generator and power converter subsystem.
%
%   BOM reference: generator 2600, power converter 6100, transformer 6200.
%
%   PORTS
%     In   1  T_gen_cmd [Nm]      commanded generator torque (HSS)
%          2  omega_g   [rad/s]   generator speed
%     Out  1  T_gen     [Nm]      actual electromagnetic torque
%          2  P_elec    [W]       electrical power delivered to the grid
%          3  P_mech    [W]       mechanical power at the generator shaft
%
%   MODEL
%     First-order torque response with magnitude and rate limits:
%
%         tau_gen * T_dot + T = T_cmd_limited
%         P_mech = T_gen * omega_g
%         P_elec = P_mech * eta_gen
%
%   SCOPE OF THIS BLOCK
%   -------------------
%   This is a TORQUE-SOURCE abstraction: the converter is assumed to deliver
%   whatever electromagnetic torque is asked of it, within limits, far faster
%   than any mechanical mode in the model (tau = 20 ms against a drivetrain
%   mode in the 1-10 Hz range). That is the standard and appropriate
%   simplification for load and control studies.
%
%   It deliberately does NOT model: dq-axis machine dynamics, converter
%   switching, grid voltage or frequency, reactive power, or fault
%   ride-through. Those need an electrical-domain model (Simscape
%   Electrical), and any grid-code compliance question is outside what this
%   model can answer.
%
%   See also WTBUILDCONTROLLER, WTBUILDMODEL.

wtAdd('simulink/Sources/In1', [sys '/T_gen_cmd'], [30 100], 'Port','1');
wtAdd('simulink/Sources/In1', [sys '/omega_g_in'],[30 300], 'Port','2');

% Torque magnitude limit (converter current rating, BOM 6100).
wtAdd('simulink/Discontinuities/Saturation', [sys '/T_limit'], [140 100], ...
    'UpperLimit','P.gen.T_max', ...
    'LowerLimit','0');

% Torque rate limit: protects the drivetrain from step demands, which would
% otherwise ring the torsional mode built in wtBuildDrivetrain.
wtAdd('simulink/Discontinuities/Rate Limiter', [sys '/T_rate'], [250 100], ...
    'RisingSlewLimit','P.gen.rateLimit', ...
    'FallingSlewLimit','-P.gen.rateLimit');

wtAdd('simulink/Continuous/Transfer Fcn', [sys '/converterLag'], [360 100], ...
    'Numerator','[1]', ...
    'Denominator','[P.gen.tau 1]');

% Mechanical power at the shaft.
wtAdd('simulink/Math Operations/Product', [sys '/P_mech_calc'], [500 200]);

% Electrical power after machine and converter losses.
wtAdd('simulink/Math Operations/Gain', [sys '/eta'], [620 200], ...
    'Gain','P.eta_gen');

wtAdd('simulink/Sinks/Out1', [sys '/T_gen_out'], [500 100], 'Port','1');
wtAdd('simulink/Sinks/Out1', [sys '/P_elec_out'],[740 200], 'Port','2');
wtAdd('simulink/Sinks/Out1', [sys '/P_mech_out'],[620 300], 'Port','3');

wtLine(sys, { ...
    'T_gen_cmd/1',    'T_limit/1'
    'T_limit/1',      'T_rate/1'
    'T_rate/1',       'converterLag/1'
    'converterLag/1', 'T_gen_out/1'
    'converterLag/1', 'P_mech_calc/1'
    'omega_g_in/1',   'P_mech_calc/2'
    'P_mech_calc/1',  'eta/1'
    'eta/1',          'P_elec_out/1'
    'P_mech_calc/1',  'P_mech_out/1'});

end
