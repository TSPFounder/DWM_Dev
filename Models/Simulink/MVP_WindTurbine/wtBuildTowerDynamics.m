function wtBuildTowerDynamics(sys)
%WTBUILDTOWERDYNAMICS  Build the tower fore-aft structural subsystem.
%
%   BOM reference: tower sections 4100/4110/4120, flange bolting 4130.
%
%   PORTS
%     In   1  F_thrust  [N]      rotor axial thrust
%     Out  1  x_t       [m]      nacelle fore-aft displacement
%          2  xdot_t    [m/s]    nacelle fore-aft velocity
%          3  a_t       [m/s^2]  nacelle fore-aft acceleration
%
%   MODEL -- single-DOF modal reduction of the first fore-aft bending mode:
%
%       m_modal * x_ddot + c * x_dot + k * x = F_thrust
%
%   with m_modal = m_topHead + 0.25*m_tower (standard cantilever
%   participation factor) and k chosen to place f1 inside the soft-stiff
%   window. See WTPARAMETERS for the derivation from BOM masses.
%
%   WHY THIS SUBSYSTEM EXISTS AT ALL
%   --------------------------------
%   Two reasons, both from the BOM's own tower entry:
%
%   1. The BOM states the tower is STIFFNESS-driven, not strength-driven:
%      f1 must avoid 1P (0.233 Hz) and 3P (0.700 Hz). This model is where
%      that separation can actually be checked -- WTRUNSIMULATION asserts it.
%
%   2. The nacelle velocity output feeds back into the aerodynamics as
%      v_rel = v_wind - xdot_t. That feedback is the dominant source of
%      damping for this mode; structural damping alone is only ~1 %. Without
%      it the tower response is unrealistically lightly damped.
%
%   The side-side mode, tower torsion, and blade flap/edge modes are not
%   modelled. Side-side matters for a full load assessment (it is even more
%   lightly damped, since it gets no aerodynamic damping) but is not needed
%   for control design.
%
%   The acceleration output corresponds to what BOM item 7270 (nacelle
%   accelerometers) actually measures, and is what a real active tower
%   damper would feed on.
%
%   See also WTBUILDMODEL, WTPARAMETERS.

wtAdd('simulink/Sources/In1', [sys '/F_thrust_in'], [30 200], 'Port','1');

% m*xddot = F - c*xdot - k*x
wtAdd('simulink/Math Operations/Sum', [sys '/SumForces'], [160 200], ...
    'Inputs','+--');

wtAdd('simulink/Math Operations/Gain', [sys '/invM'], [260 200], ...
    'Gain','1/P.m_towerModal');

wtAdd('simulink/Continuous/Integrator', [sys '/xdot_int'], [360 200], ...
    'InitialCondition','0');

wtAdd('simulink/Continuous/Integrator', [sys '/x_int'], [470 200], ...
    'InitialCondition','0');

wtAdd('simulink/Math Operations/Gain', [sys '/damping'], [360 340], ...
    'Gain','P.C_tower');

wtAdd('simulink/Math Operations/Gain', [sys '/stiffness'], [470 420], ...
    'Gain','P.K_tower');

wtAdd('simulink/Sinks/Out1', [sys '/x_out'],    [600 200], 'Port','1');
wtAdd('simulink/Sinks/Out1', [sys '/xdot_out'], [600 120], 'Port','2');
wtAdd('simulink/Sinks/Out1', [sys '/a_out'],    [360 100], 'Port','3');

wtLine(sys, { ...
    'F_thrust_in/1', 'SumForces/1'
    'damping/1',     'SumForces/2'
    'stiffness/1',   'SumForces/3'
    'SumForces/1',   'invM/1'
    'invM/1',        'xdot_int/1'
    'invM/1',        'a_out/1'
    'xdot_int/1',    'x_int/1'
    'xdot_int/1',    'damping/1'
    'xdot_int/1',    'xdot_out/1'
    'x_int/1',       'stiffness/1'
    'x_int/1',       'x_out/1'});

end
