function out = wtRunSimulation(scenario)
%WTRUNSIMULATION  Build, run and plot the 3 MW wind turbine model.
%
%   OUT = WTRUNSIMULATION()             runs the default wind-ramp scenario.
%   OUT = WTRUNSIMULATION('step')       step from below to above rated.
%   OUT = WTRUNSIMULATION('ramp')       slow ramp through both regions.
%   OUT = WTRUNSIMULATION('turbulent')  synthetic turbulent series.
%   OUT = WTRUNSIMULATION('gust')       IEC-style extreme operating gust.
%
%   Returns a struct of named, unit-tagged time series -- not a raw matrix,
%   so a downstream script cannot silently index the wrong column.
%
%   This is the entry point. Run this file, nothing else.
%
%   -----------------------------------------------------------------------
%   LIMITATIONS -- read before drawing conclusions from any result
%   -----------------------------------------------------------------------
%   This is a CONTROL-DESIGN AND BEHAVIOURAL model, not a load-certification
%   model. Specifically:
%
%   * The Cp/Ct surface is a generic empirical fit (see WTCP), not BEM output
%     from the actual blade geometry in the BOM. Absolute power capture is
%     therefore indicative only; the SHAPE is right, the numbers are not
%     certified.
%   * Structure is reduced to two DOF: drivetrain torsion and tower fore-aft.
%     No blade flap/edge modes, no tower side-side, no torsion. Blade loads
%     cannot be extracted from this model.
%   * The wind is a single point value. No rotor-plane sampling, no wind
%     shear, no tower shadow -- so 1P and 3P load harmonics do not appear at
%     all, which is precisely what the tower frequency separation exists to
%     avoid. This model can check the separation exists; it cannot show the
%     consequence of getting it wrong.
%   * Electrically the generator is a torque source. No grid model, so no
%     fault ride-through, no reactive power, no grid-code questions.
%   * The safety chain is not modelled -- deliberately, see WTBUILDCONTROLLER.
%
%   For certification (IEC 61400-1) use an aeroelastic code -- OpenFAST,
%   Bladed, HAWC2 -- driven by the full design load case set.
%
%   See also WTBUILDMODEL, WTPARAMETERS.

if nargin < 1 || isempty(scenario), scenario = 'ramp'; end

%% ------------------------------------------------------------------------
%  Parameters and tables
%  ------------------------------------------------------------------------
P  = wtParameters();
AT = wtGenerateAeroTables(P);

fprintf('\n=== Derived dynamics (all traced to BOM values) ===\n');
fprintf('  Rotor inertia      J_r     = %10.3e kg m^2\n', P.J_rotor);
fprintf('  Generator inertia  J_g     = %10.3e kg m^2\n', P.J_gen);
fprintf('  Shaft stiffness    K_s     = %10.3e Nm/rad\n', P.K_shaft);
fprintf('  Drivetrain mode    f_dt    = %10.3f Hz\n', P.omega_dt/(2*pi));
fprintf('  Tower modal mass   m_t     = %10.3e kg\n', P.m_towerModal);
fprintf('  Tower stiffness    K_t     = %10.3e N/m\n', P.K_tower);
fprintf('  Tower frequency    f_1     = %10.3f Hz\n', P.f_tower);
fprintf('  Rated gen torque   T_rated = %10.3e Nm\n', P.T_genRated);
fprintf('  Region II gain     K_opt   = %10.4f Nm/(rad/s)^2\n', P.K_opt);
fprintf('  Cp table peak      Cp_max  = %10.3f at lambda = %.2f\n', ...
    AT.check.Cp_peak, AT.check.lambda_peak);

%% ------------------------------------------------------------------------
%  Design check: the BOM's soft-stiff tower requirement
%  ------------------------------------------------------------------------
% The BOM states the tower is stiffness-driven and f1 must sit between 1P
% and 3P. Assert it rather than assume it -- this is the one structural
% requirement this model is actually able to check.
margin = 0.10;   % 10 % clearance either side
lo = P.f_1P*(1+margin);
hi = P.f_3P*(1-margin);
fprintf('\n=== Soft-stiff tower check ===\n');
fprintf('  Required window : %.3f - %.3f Hz (1P +10%%, 3P -10%%)\n', lo, hi);
fprintf('  Actual f_tower  : %.3f Hz\n', P.f_tower);
if P.f_tower > lo && P.f_tower < hi
    fprintf('  RESULT: PASS\n');
else
    warning('wtRunSimulation:towerFrequency', ...
        ['Tower f1 = %.3f Hz is OUTSIDE the soft-stiff window %.3f-%.3f Hz. ' ...
         'The rotor will excite the tower mode directly at 1P or 3P.'], ...
        P.f_tower, lo, hi);
end

%% ------------------------------------------------------------------------
%  Wind scenario
%  ------------------------------------------------------------------------
T  = P.sim.stopTime;
dt = 0.05;
t  = (0:dt:T)';

switch lower(scenario)
    case 'step'
        v = 8*ones(size(t));
        v(t >= T/2) = 16;

    case 'ramp'
        % Sweep the whole operating range so both control regions and both
        % transitions are exercised in one run.
        v = interp1([0 0.15*T 0.55*T 0.85*T T], ...
                    [4   6      13      20   22], t, 'linear');

    case 'turbulent'
        % Low-order synthetic turbulence: a mean plus filtered noise. NOT a
        % Kaimal/von Karman field -- adequate to shake the controller, not
        % adequate for a fatigue spectrum.
        % Fixed seed so the scenario is repeatable. RNG arrived in R2011a;
        % fall back to the legacy syntax on anything older.
        try
            rng(0);
        catch
            randn('state', 0); %#ok<RAND>
        end
        mean_v = 12;
        Ti     = 0.16;                     % IEC Class A turbulence intensity
        white  = randn(size(t));
        L      = 20;                       % [s] correlation length
        b      = dt/L;
        colored = filter(b, [1 -(1-b)], white);
        colored = colored / std(colored);
        v = mean_v * (1 + Ti*colored);

    case 'gust'
        % IEC extreme operating gust (EOG) shape superimposed on the rated
        % wind: the classic pitch-controller stress test.
        v      = P.v_rated*ones(size(t));
        Vgust  = 6.0;                       % [m/s] gust magnitude
        Tg     = 10.5;                      % [s]   gust duration
        t0     = T/2;
        inG    = t >= t0 & t <= t0+Tg;
        tau    = (t(inG)-t0)/Tg;
        v(inG) = v(inG) - 0.37*Vgust*sin(3*pi*tau).*(1 - cos(2*pi*tau));

    otherwise
        error('wtRunSimulation:badScenario', ...
            'Unknown scenario "%s". Use step|ramp|turbulent|gust.', scenario);
end

v = max(v, 0.5);   % the aero block guards this too, but keep the input sane

% Wind direction: a slow 20 deg swing so the yaw system does something.
windDir = (20*pi/180)*sin(2*pi*t/(T));

windSignal    = [t, v];        %#ok<NASGU>  read by the From Workspace block
windDirSignal = [t, windDir];  %#ok<NASGU>

%% ------------------------------------------------------------------------
%  Build and run
%  ------------------------------------------------------------------------
modelName = wtBuildModel('wtTurbine3MW', P, AT);

% The From Workspace blocks resolve against the base workspace, so the two
% drive signals must be pushed there. P and AT deliberately are NOT -- they
% live in the model workspace so the saved model stays self-contained.
assignin('base', 'windSignal',    windSignal);
assignin('base', 'windDirSignal', windDirSignal);

fprintf('\nSimulating "%s" scenario for %g s ...\n', scenario, T);

% ReturnWorkspaceOutputs must be requested explicitly. Older releases (R2011a
% era) return logged variables straight to the base workspace from a
% single-output sim() call rather than packaging them in a SimulationOutput
% object; asking for the object makes the behaviour the same either way.
simOut = sim(modelName, ...
    'StopTime',                num2str(T), ...
    'ReturnWorkspaceOutputs',  'on');

%% ------------------------------------------------------------------------
%  Unpack logged signals
%  ------------------------------------------------------------------------
% Channel order is fixed by the Mux wiring in wtBuildModel. Assert the width
% so that inserting a channel there without updating here fails loudly
% instead of mislabelling every plot below.
% Belt and braces across releases: take it from the SimulationOutput object
% when there is one, otherwise from the base workspace where older releases
% leave it.
if isa(simOut, 'Simulink.SimulationOutput')
    simLog = simOut.get('wtLog');
elseif evalin('base', 'exist(''wtLog'',''var'')')
    simLog = evalin('base', 'wtLog');
else
    error('wtRunSimulation:noLog', ...
        ['Simulation produced no "wtLog" variable. Check that the To Workspace ' ...
         'block in the model is named wtLog and its Save format is ' ...
         '"Structure With Time".']);
end

Y   = simLog.signals.values;
tt  = simLog.time;

nExpected = 12;
assert(size(Y,2) == nExpected, 'wtRunSimulation:logWidth', ...
    ['Logged %d channels, expected %d. The LogMux wiring in wtBuildModel ' ...
     'and the unpacking in wtRunSimulation have drifted apart.'], ...
    size(Y,2), nExpected);

out.t        = tt;
out.v_rel    = Y(:,1);    out.units.v_rel   = 'm/s';
out.omega_r  = Y(:,2);    out.units.omega_r = 'rad/s';
out.omega_g  = Y(:,3);    out.units.omega_g = 'rad/s';
out.beta     = Y(:,4);    out.units.beta    = 'rad';
out.T_aero   = Y(:,5);    out.units.T_aero  = 'Nm';
out.T_gen    = Y(:,6);    out.units.T_gen   = 'Nm';
out.P_elec   = Y(:,7);    out.units.P_elec  = 'W';
out.x_t      = Y(:,8);    out.units.x_t     = 'm';
out.Cp       = Y(:,9);    out.units.Cp      = '-';
out.lambda   = Y(:,10);   out.units.lambda  = '-';
out.F_thrust = Y(:,11);   out.units.F_thrust= 'N';
out.yawError = Y(:,12);   out.units.yawError= 'rad';
out.v_wind   = interp1(t, v, tt);
out.P        = P;

%% ------------------------------------------------------------------------
%  Post-run sanity checks
%  ------------------------------------------------------------------------
fprintf('\n=== Post-run checks ===\n');
checks = {};

pk = max(out.P_elec);
checks{end+1} = {sprintf('Peak power %.2f MW <= 1.10 x rated', pk/1e6), ...
                 pk <= 1.10*P.P_rated};

wg = max(out.omega_g);
checks{end+1} = {sprintf('Peak gen speed %.1f rad/s <= trip %.1f', ...
                 wg, P.sup.overspeedTrip), wg <= P.sup.overspeedTrip};

cpmax = max(out.Cp);
checks{end+1} = {sprintf('Peak Cp %.3f <= Betz 0.593', cpmax), cpmax <= 0.593};

bmax = max(out.beta)*180/pi;
checks{end+1} = {sprintf('Peak pitch %.1f deg <= 90', bmax), bmax <= 90.001};

for k = 1:numel(checks)
    fprintf('  [%s] %s\n', ternary(checks{k}{2},'PASS','FAIL'), checks{k}{1});
end

%% ------------------------------------------------------------------------
%  Plots
%  ------------------------------------------------------------------------
figure('Name', sprintf('3 MW Turbine - %s', scenario), ...
       'Position', [80 80 1150 800]);

subplot(3,2,1);
plot(tt, out.v_wind, 'LineWidth', 1.1); grid on;
ylabel('wind [m/s]'); title('Wind speed');
wtHLine(P.v_rated, 'rated');

subplot(3,2,2);
plot(tt, out.P_elec/1e6, 'LineWidth', 1.1); grid on;
ylabel('P_{elec} [MW]'); title('Electrical power');
wtHLine(P.P_rated/1e6, 'rated');

subplot(3,2,3);
plot(tt, out.omega_r*60/(2*pi), 'LineWidth', 1.1); grid on;
ylabel('\omega_r [rpm]'); title('Rotor speed');
wtHLine(P.omega_rMax*60/(2*pi), 'max');

subplot(3,2,4);
plot(tt, out.beta*180/pi, 'LineWidth', 1.1); grid on;
ylabel('\beta [deg]'); title('Collective pitch');

subplot(3,2,5);
plot(tt, out.Cp, 'LineWidth', 1.1); grid on;
ylabel('C_p [-]'); xlabel('time [s]'); title('Power coefficient');
wtHLine(P.Cp_max, 'C_{p,max}');

subplot(3,2,6);
plot(tt, out.x_t*1000, 'LineWidth', 1.1); grid on;
ylabel('x_{tower} [mm]'); xlabel('time [s]');
title('Tower fore-aft deflection');

fprintf('\nDone.\n');

end

% -------------------------------------------------------------------------
function s = ternary(cond, a, b)
if cond
    s = a;
else
    s = b;
end
end

% -------------------------------------------------------------------------
function wtHLine(yval, txt)
%WTHLINE  Horizontal reference line with a label.
%
%   Stands in for YLINE, which only exists from R2018b. Drawn with PLOT
%   against the current x-limits, which is available in every release this
%   code targets.

xl = get(gca, 'XLim');
washHeld = ishold;
hold on;
plot(xl, [yval yval], 'k--', 'LineWidth', 0.75);
if nargin > 1 && ~isempty(txt)
    text(xl(1) + 0.02*diff(xl), yval, txt, ...
        'VerticalAlignment', 'bottom', 'FontSize', 8);
end
if ~washHeld
    hold off;
end
end
