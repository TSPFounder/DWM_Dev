function wtBuildYawSystem(sys)
%WTBUILDYAWSYSTEM  Build the yaw drive and alignment subsystem.
%
%   BOM reference: yaw bearing 3100, yaw drives 3200, yaw brake 3300,
%   yaw position encoder 7260.
%
%   PORTS
%     In   1  windDir     [rad]  wind direction (inertial frame)
%     Out  1  nacelleYaw  [rad]  nacelle azimuth
%          2  yawError    [rad]  windDir - nacelleYaw
%          3  cosYawErr   [-]    cos(yawError), the wind-speed derate factor
%
%   MODEL
%     A deadband on yaw error, then a rate-limited first-order tracker:
%
%         yawError  = windDir - nacelleYaw
%         demand    = yawError outside the deadband, else 0
%         yaw rate  limited to P.yaw.rate
%
%   THE DEADBAND AND THE RATE LIMIT BOTH MATTER
%   -------------------------------------------
%   The deadband (8 deg here) stops the nacelle hunting continuously on
%   turbulent direction fluctuations, which would consume yaw-drive duty
%   cycle for no energy gain. The rate limit (0.5 deg/s, BOM 3200) exists
%   because yawing a spinning rotor precesses it: the gyroscopic moment
%   transmitted to the main shaft and tower rises with yaw rate, so yaw
%   speed is limited by drivetrain load, not by drive capability.
%
%   The cos(yawError) output is how misalignment couples back into power:
%   effective wind speed normal to the rotor is v*cos(yawError). Some
%   references use cos^3 on power (equivalent to cos on velocity cubed);
%   applying cos to VELOCITY here and letting the aero block cube it gives
%   the same result while keeping each block's physics local and honest.
%
%   NOT MODELLED: cable twist accumulation and the untwist routine. The BOM
%   flags that as functionally important (item 4210 -- failure of the
%   untwist logic tears the main power cable), but it is supervisory
%   bookkeeping over hours of operation, not dynamics, and belongs in the
%   discrete supervisory controller rather than here.
%
%   See also WTBUILDMODEL, WTBUILDCONTROLLER.

wtAdd('simulink/Sources/In1', [sys '/windDir'], [30 160], 'Port','1');

wtAdd('simulink/Math Operations/Sum', [sys '/SumYawErr'], [150 160], ...
    'Inputs','+-');

% Deadband: no yaw demand until misalignment is worth correcting.
wtAdd('simulink/Discontinuities/Dead Zone', [sys '/deadband'], [250 160], ...
    'LowerValue','-P.yaw.deadband', ...
    'UpperValue','P.yaw.deadband');

% Proportional demand, then hardware rate limit.
wtAdd('simulink/Math Operations/Gain', [sys '/yawGain'], [350 160], ...
    'Gain','1/P.yaw.tau');

wtAdd('simulink/Discontinuities/Saturation', [sys '/yawRateLim'], [450 160], ...
    'UpperLimit','P.yaw.rate', ...
    'LowerLimit','-P.yaw.rate');

wtAdd('simulink/Continuous/Integrator', [sys '/yaw_int'], [550 160], ...
    'InitialCondition','0');

% cos(yaw error) -- the velocity derate seen by the rotor.
wtAdd('simulink/Math Operations/Trigonometric Function', [sys '/cosErr'], ...
    [350 300], 'Operator','cos');

wtAdd('simulink/Sinks/Out1', [sys '/nacelleYaw_out'], [660 160], 'Port','1');
wtAdd('simulink/Sinks/Out1', [sys '/yawError_out'],   [250 60],  'Port','2');
wtAdd('simulink/Sinks/Out1', [sys '/cosYawErr_out'],  [470 300], 'Port','3');

wtLine(sys, { ...
    'windDir/1',     'SumYawErr/1'
    'yaw_int/1',     'SumYawErr/2'
    'SumYawErr/1',   'deadband/1'
    'deadband/1',    'yawGain/1'
    'yawGain/1',     'yawRateLim/1'
    'yawRateLim/1',  'yaw_int/1'
    'yaw_int/1',     'nacelleYaw_out/1'
    'SumYawErr/1',   'yawError_out/1'
    'SumYawErr/1',   'cosErr/1'
    'cosErr/1',      'cosYawErr_out/1'});

end
