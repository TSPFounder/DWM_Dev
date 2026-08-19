function wtBuildSupervisor(sys)
%WTBUILDSUPERVISOR  Build the supervisory state machine subsystem.
%
%   BOM reference: main turbine controller 7100 (supervisory layer),
%   generator-speed encoder 7240.
%
%   PORTS
%     In   1  omega_g     [rad/s]  generator speed (BOM 7240)
%          2  T_gen_ctrl  [Nm]     torque demand from the Controller
%          3  beta_ctrl   [rad]    pitch demand from the Controller
%     Out  1  T_gen_cmd   [Nm]     torque demand passed to GenConverter
%          2  beta_cmd    [rad]    pitch demand passed to PitchActuator
%          3  state       [-]      0 Parked, 1 Startup, 2 Generating
%
%   WHAT THIS IS FOR
%   ----------------
%   WTBUILDCONTROLLER lists "start-up/shutdown sequencing" under NOT
%   INCLUDED. This is the start-up half of that omission. Without it the
%   model can only ever be started at or above cut-in, because the rotor
%   speed integrator has to be initialised somewhere and the Region II
%   torque law has nothing to say below cut-in.
%
%   WHY THIS IS A SEPARATE LAYER FROM THE REGION LOGIC
%   --------------------------------------------------
%   WTBUILDCONTROLLER deliberately has NO mode switch: the Region II/III
%   transition is implicit, so there is no switching transient. That
%   reasoning is intact and this subsystem must not undo it. A supervisor
%   answers a different question -- is the machine parked, running up, or
%   generating -- and the Region II/III behaviour is entirely contained
%   inside the Generating state, unchanged and still switch-free.
%
%   THE THREE STATES
%   ----------------
%     0  PARKED      beta = feather, T_gen = 0.
%                    Feathered blades produce a NEGATIVE torque coefficient
%                    at every low tip-speed ratio, so a parked rotor stays
%                    parked rather than idling.
%
%     1  STARTUP     beta = P.sup.beta_start, T_gen = 0.
%                    Pitch to the start position and let the rotor
%                    accelerate aerodynamically. The generator is left OFF,
%                    not merely low: any torque demand here is a brake
%                    fighting the only thing accelerating the rotor.
%
%     2  GENERATING  beta and T_gen pass through from the Controller.
%                    This is the model as it behaved before this subsystem
%                    existed.
%
%   TRANSITIONS, AND WHY THEY LATCH
%   -------------------------------
%     PARKED -> STARTUP     at t = P.sup.t_start (a Step, monotonic, so the
%                           command latches by construction).
%
%     STARTUP -> GENERATING when omega_g crosses P.sup.omega_gOn, detected
%                           by a RELAY rather than a comparison. The relay
%                           drops out only below P.sup.omega_gOff, which is
%                           the hysteresis a real supervisor uses: a bare
%                           ">=" would chatter between states on the first
%                           torque dip after cut-in, and every chatter cycle
%                           would step the pitch demand between beta_start
%                           and the controller output.
%
%   The generating gate is ANDed with the start command (a Product, since
%   both are 0/1) so that a model initialised above cut-in cannot skip
%   straight to Generating while still parked.
%
%   NO STATEFLOW, DELIBERATELY. This model uses none and the R2011a licence
%   does not include it. Three states with latching transitions are well
%   within what a Step, a Relay and two Switch blocks express, and they stay
%   readable in the block diagram rather than hiding behind a chart.
%
%   NOT INCLUDED
%   ------------
%   Shutdown sequencing, cut-out handling, and the overspeed trip. P.sup
%   already carries v_cutOut and overspeedTrip for those; the trip in
%   particular belongs with the safety chain (BOM 7400), which
%   WTBUILDCONTROLLER explains must stay independent of controller software
%   and therefore does not belong in this subsystem either.
%
%   See also WTBUILDCONTROLLER, WTBUILDMODEL, WTPARAMETERS.

%% Inputs ------------------------------------------------------------------
wtAdd('simulink/Sources/In1', [sys '/omega_g'],    [30 120], 'Port','1');
wtAdd('simulink/Sources/In1', [sys '/T_gen_ctrl'], [30 300], 'Port','2');
wtAdd('simulink/Sources/In1', [sys '/beta_ctrl'],  [30 460], 'Port','3');

%% ------------------------------------------------------------------------
%  STATE LOGIC
%  ------------------------------------------------------------------------
% Start command. A Step is the whole PARKED -> STARTUP latch: it is 0 before
% t_start and 1 after, and never returns.
wtAdd('simulink/Sources/Step', [sys '/startCmd'], [160 220], ...
    'Time','P.sup.t_start', ...
    'Before','0', ...
    'After','1');

% Cut-in detection with hysteresis. See the header for why this is a relay
% and not a relational operator.
wtAdd('simulink/Discontinuities/Relay', [sys '/cutInRelay'], [160 120], ...
    'OnSwitchValue','P.sup.omega_gOn', ...
    'OffSwitchValue','P.sup.omega_gOff', ...
    'OnOutputValue','1', ...
    'OffOutputValue','0');

% genGate = startCmd AND cutIn. Both operands are 0/1, so a product is the
% AND, and it stays a double signal -- no boolean/double mixing to configure.
wtAdd('simulink/Math Operations/Product', [sys '/genGate'], [300 160]);

% state = startCmd + genGate  ->  0 parked, 1 startup, 2 generating.
wtAdd('simulink/Math Operations/Sum', [sys '/stateSum'], [430 200], ...
    'Inputs','++');

wtAdd('simulink/Sinks/Out1', [sys '/state'], [560 200], 'Port','3');

%% ------------------------------------------------------------------------
%  TORQUE PATH
%  ------------------------------------------------------------------------
% Gating by multiplication rather than switching to a zero constant: under
% the Region II law the demand is already small at low speed, so this is a
% hard OFF rather than a discontinuous jump away from a large value.
wtAdd('simulink/Math Operations/Product', [sys '/torqueGate'], [430 300]);

wtAdd('simulink/Sinks/Out1', [sys '/T_gen_cmd'], [560 300], 'Port','1');

%% ------------------------------------------------------------------------
%  PITCH PATH
%  ------------------------------------------------------------------------
wtAdd('simulink/Sources/Constant', [sys '/betaPark'], [160 620], ...
    'Value','P.sup.beta_park');

wtAdd('simulink/Sources/Constant', [sys '/betaStart'], [160 540], ...
    'Value','P.sup.beta_start');

% Two cascaded switches read inner-to-outer: the inner one picks the
% non-running pitch (feather or start position), the outer one hands control
% to the Controller once generating. Threshold 0.5 on signals that are
% exactly 0 or 1.
wtAdd('simulink/Signal Routing/Switch', [sys '/SwStart'], [300 560], ...
    'Criteria','u2 >= Threshold', 'Threshold','0.5');

wtAdd('simulink/Signal Routing/Switch', [sys '/SwGen'], [430 480], ...
    'Criteria','u2 >= Threshold', 'Threshold','0.5');

wtAdd('simulink/Sinks/Out1', [sys '/beta_cmd'], [560 480], 'Port','2');

%% Signal routing ----------------------------------------------------------
wtLine(sys, { ...
    % --- state logic ---
    'omega_g/1',      'cutInRelay/1'
    'cutInRelay/1',   'genGate/1'
    'startCmd/1',     'genGate/2'
    'startCmd/1',     'stateSum/1'
    'genGate/1',      'stateSum/2'
    'stateSum/1',     'state/1'

    % --- torque: zero unless generating ---
    'T_gen_ctrl/1',   'torqueGate/1'
    'genGate/1',      'torqueGate/2'
    'torqueGate/1',   'T_gen_cmd/1'

    % --- pitch: feather -> start position -> controller ---
    'betaStart/1',    'SwStart/1'
    'startCmd/1',     'SwStart/2'
    'betaPark/1',     'SwStart/3'
    'beta_ctrl/1',    'SwGen/1'
    'genGate/1',      'SwGen/2'
    'SwStart/1',      'SwGen/3'
    'SwGen/1',        'beta_cmd/1'});

end
