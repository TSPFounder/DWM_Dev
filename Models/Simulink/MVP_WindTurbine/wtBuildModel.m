function modelName = wtBuildModel(modelName, P, AT)
%WTBUILDMODEL  Build the complete 3 MW wind turbine Simulink model from script.
%
%   MODELNAME = WTBUILDMODEL() builds and saves the default model.
%   MODELNAME = WTBUILDMODEL(NAME, P, AT) builds it under a given name with a
%   supplied parameter struct and aerodynamic tables.
%
%   The model is assembled from eight subsystems, each built by its own
%   function so that a change to one component's physics touches one file:
%
%       RotorAero        wtBuildRotorAero          BOM 1000
%       Drivetrain       wtBuildDrivetrain         BOM 2100-2400, 2600
%       PitchActuator    wtBuildPitchActuator      BOM 1300-1320
%       GenConverter     wtBuildGeneratorConverter BOM 2600, 6100
%       TowerDynamics    wtBuildTowerDynamics      BOM 4100-4130
%       YawSystem        wtBuildYawSystem          BOM 3100-3300
%       Controller       wtBuildController         BOM 7100-7250
%       Supervisor       wtBuildSupervisor         BOM 7100 (supervisory)
%
%   TOP-LEVEL SIGNAL FLOW
%   ---------------------
%       v_free --x-- cos(yawErr) --> v_eff --(-)-- xdot_t --> v_rel
%                                                              |
%                                    +-------------------------+
%                                    v
%       [RotorAero] --T_aero--> [Drivetrain] --omega_g--> [Controller]
%             |                       ^                        |
%             |                       |                    T_gen_cmd
%             +--F_thrust--> [Tower]  |                        v
%                               |     +----T_gen----- [GenConverter]
%                             xdot_t
%             beta <-- [PitchActuator] <--beta_cmd-- [Supervisor] <-- [Controller]
%
%   THE TWO FEEDBACK PATHS WORTH NOTING
%   -----------------------------------
%   1. xdot_t is subtracted from wind speed before the aerodynamics. This is
%      the aeroelastic coupling that damps the tower mode; without it the
%      tower rings at only its ~1 % structural damping.
%   2. beta is fed back to the controller, not just forward to the rotor,
%      because the pitch gain schedule is a function of actual pitch angle.
%
%   No algebraic loops exist: every feedback path passes through an
%   integrator (drivetrain, tower) or a first-order lag (pitch, converter).
%
%   THE SUPERVISOR SITS BETWEEN THE CONTROLLER AND THE PLANT
%   --------------------------------------------------------
%   Both controller outputs are routed through Supervisor before they reach
%   GenConverter and PitchActuator, so the flow drawn above is the
%   GENERATING state. In Parked and Startup the supervisor overrides both
%   demands and the Controller drives nothing. This adds no algebraic loop:
%   the supervisor is pure feedthrough gating, and every path into it
%   already passes through an integrator or a lag.
%
%   See also WTPARAMETERS, WTGENERATEAEROTABLES, WTRUNSIMULATION.

if nargin < 1 || isempty(modelName), modelName = 'wtTurbine3MW'; end
if nargin < 2 || isempty(P),         P  = wtParameters();        end
if nargin < 3 || isempty(AT),        AT = wtGenerateAeroTables(P); end

%% Make sure the Simulink library is available ----------------------------
% Older releases do not auto-load it on the first add_block, which surfaces
% as "There is no block named 'simulink/Sources/In1'". wtAdd also falls back
% to built-in/ block types, but loading the library keeps the readable
% library paths working as the primary route.
if ~any(strcmp(find_system('type', 'block_diagram'), 'simulink'))
    load_system('simulink');
end

%% Start from a clean slate ------------------------------------------------
% .slx did not exist before R2012a (MATLAB 7.14); older releases save .mdl.
% Delete whichever this installation would write, so a rebuild never silently
% loads a stale model of the other extension.
if verLessThan('matlab', '7.14')
    modelExt = '.mdl';
else
    modelExt = '.slx';
end

if any(strcmp(find_system('type', 'block_diagram'), modelName))
    close_system(modelName, 0);
end
for ext = {'.mdl', '.slx'}
    if exist([modelName ext{1}], 'file')
        delete([modelName ext{1}]);
    end
end

new_system(modelName, 'Model');
open_system(modelName);

%% Parameters live in the MODEL workspace ---------------------------------
% Not the base workspace: this makes the saved .slx self-contained, so it
% still runs for someone who opens it without first running a setup script.
% A model that silently depends on base-workspace variables is the classic
% "works on my machine" Simulink failure.
% Method-call form (mws.assignin) rather than the function form
% assignin(mws,...): the method form is what the ModelWorkspace API has
% supported consistently since R2011a.
mws = get_param(modelName, 'ModelWorkspace');
mws.assignin('P',  P);
mws.assignin('AT', AT);

%% Solver configuration ----------------------------------------------------
% Stiff solver: the drivetrain torsional mode (order 10 Hz) is fast relative
% to the tower mode (0.32 Hz) and the rotor speed dynamics (order 0.01 Hz).
% A non-stiff explicit solver takes tiny steps for the whole run.
set_param(modelName, ...
    'Solver',        P.sim.solver, ...
    'StopTime',      num2str(P.sim.stopTime), ...
    'MaxStep',       num2str(P.sim.maxStep), ...
    'RelTol',        '1e-4', ...
    'AbsTol',        '1e-6', ...
    'SaveFormat',    'StructureWithTime');

%% ------------------------------------------------------------------------
%  Subsystems
%  ------------------------------------------------------------------------
subs = { ...
    'RotorAero',     [420  80 560 200], @wtBuildRotorAero
    'Drivetrain',    [640  80 780 200], @wtBuildDrivetrain
    'GenConverter',  [860 240 1000 340], @wtBuildGeneratorConverter
    'Controller',    [640 420 780 540], @wtBuildController
    'PitchActuator', [420 420 560 500], @wtBuildPitchActuator
    'TowerDynamics', [420 240 560 330], @wtBuildTowerDynamics
    'YawSystem',     [180 560 320 650], @wtBuildYawSystem
    'Supervisor',    [860 420 1000 540], @wtBuildSupervisor};

for k = 1:size(subs,1)
    name    = subs{k,1};
    pos     = subs{k,2};
    builder = subs{k,3};

    % 'SubSystem' with the capital S is the actual BlockType string.
    add_block('built-in/SubSystem', [modelName '/' name], 'Position', pos);
    builder([modelName '/' name]);
end

%% ------------------------------------------------------------------------
%  Wind input
%  ------------------------------------------------------------------------
% From Workspace so the same model can be driven by a step, a turbulent
% series, or a measured record without rebuilding. WTRUNSIMULATION populates
% these variables.
wtAdd('simulink/Sources/From Workspace', [modelName '/WindSpeed'], [40 100], ...
    'VariableName','windSignal', ...
    'SampleTime','0');

wtAdd('simulink/Sources/From Workspace', [modelName '/WindDirection'], [40 580], ...
    'VariableName','windDirSignal', ...
    'SampleTime','0');

%% Yaw derate: v_eff = v_free * cos(yawError) -----------------------------
wtAdd('simulink/Math Operations/Product', [modelName '/YawDerate'], [200 120]);

%% Relative wind: v_rel = v_eff - xdot_nacelle ----------------------------
wtAdd('simulink/Math Operations/Sum', [modelName '/RelativeWind'], [310 120], ...
    'Inputs','+-');

%% ------------------------------------------------------------------------
%  Logging
%  ------------------------------------------------------------------------
% One Mux into one To Workspace keeps the model readable. Channel order is
% documented here AND asserted in wtRunSimulation, so a future edit that
% inserts a channel cannot silently mislabel every plot.
wtAdd('simulink/Signal Routing/Mux', [modelName '/LogMux'], [1120 100 1130 640], ...
    'Inputs','13', 'DisplayOption','bar');

wtAdd('simulink/Sinks/To Workspace', [modelName '/LogOut'], [1200 350], ...
    'VariableName','wtLog', ...
    'SaveFormat','StructureWithTime', ...
    'SampleTime','-1');

%% ------------------------------------------------------------------------
%  Top-level signal routing
%  ------------------------------------------------------------------------
wtLine(modelName, { ...
    % --- wind conditioning ---
    'WindSpeed/1',      'YawDerate/1'
    'YawSystem/3',      'YawDerate/2'
    'YawDerate/1',      'RelativeWind/1'
    'TowerDynamics/2',  'RelativeWind/2'
    'WindDirection/1',  'YawSystem/1'

    % --- aerodynamics ---
    'RelativeWind/1',   'RotorAero/1'
    'Drivetrain/1',     'RotorAero/2'
    'PitchActuator/1',  'RotorAero/3'

    % --- structure and drivetrain ---
    'RotorAero/1',      'Drivetrain/1'
    'RotorAero/2',      'TowerDynamics/1'
    'GenConverter/1',   'Drivetrain/2'

    % --- electrical ---
    'Supervisor/1',     'GenConverter/1'
    'Drivetrain/2',     'GenConverter/2'

    % --- control ---
    'Drivetrain/2',     'Controller/1'
    'WindSpeed/1',      'Controller/2'
    'PitchActuator/1',  'Controller/3'
    'Supervisor/2',     'PitchActuator/1'

    % --- supervisory gating ---
    % The Controller no longer reaches the plant directly. Parked and
    % Startup override both demands; Generating passes them through
    % untouched. See wtBuildSupervisor.
    'Drivetrain/2',     'Supervisor/1'
    'Controller/1',     'Supervisor/2'
    'Controller/2',     'Supervisor/3'

    % --- logging (channel order must match wtRunSimulation) ---
    'RelativeWind/1',   'LogMux/1'      % 1  v_rel     [m/s]
    'Drivetrain/1',     'LogMux/2'      % 2  omega_r   [rad/s]
    'Drivetrain/2',     'LogMux/3'      % 3  omega_g   [rad/s]
    'PitchActuator/1',  'LogMux/4'      % 4  beta      [rad]
    'RotorAero/1',      'LogMux/5'      % 5  T_aero    [Nm]
    'GenConverter/1',   'LogMux/6'      % 6  T_gen     [Nm]
    'GenConverter/2',   'LogMux/7'      % 7  P_elec    [W]
    'TowerDynamics/1',  'LogMux/8'      % 8  x_t       [m]
    'RotorAero/3',      'LogMux/9'      % 9  Cp        [-]
    'RotorAero/4',      'LogMux/10'     % 10 lambda    [-]
    'RotorAero/2',      'LogMux/11'     % 11 F_thrust  [N]
    'YawSystem/2',      'LogMux/12'     % 12 yawError  [rad]
    'Supervisor/3',     'LogMux/13'     % 13 state     [-] 0/1/2
    'LogMux/1',         'LogOut/1'});

%% ------------------------------------------------------------------------
%  Tidy and save
%  ------------------------------------------------------------------------
% Auto-arrange is cosmetic and only exists from R2018b. Every block already
% has an explicit scripted position, so losing it costs nothing on older
% releases -- hence a swallowed catch rather than a warning.
try
    Simulink.BlockDiagram.arrangeSystem(modelName);
catch
end

save_system(modelName);

fprintf('Built and saved model: %s%s\n', modelName, modelExt);
fprintf('  Subsystems : %d\n', size(subs,1));
fprintf('  J_rotor    : %.3e kg m^2   (from BOM blade mass %g kg)\n', ...
    P.J_rotor, P.m_blade);
fprintf('  K_shaft    : %.3e Nm/rad   (from BOM main shaft geometry)\n', P.K_shaft);
fprintf('  f_tower    : %.3f Hz       (1P = %.3f, 3P = %.3f)\n', ...
    P.f_tower, P.f_1P, P.f_3P);
fprintf('  K_opt      : %.4f Nm/(rad/s)^2\n', P.K_opt);

end
