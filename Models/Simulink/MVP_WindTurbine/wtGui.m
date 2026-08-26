function wtGui
%WTGUI  Interface for running and exporting the 3 MW turbine model.
%
%   WTGUI opens a window with a scenario selector, the six diagnostic plots,
%   the post-run checks, and a one-click export to the DWM world-package
%   CSVs. Default scenario is RAMP.
%
%   WHY PROGRAMMATIC uicontrol RATHER THAN GUIDE
%   --------------------------------------------
%   GUIDE splits an interface across a .m and a binary .fig that must travel
%   together and cannot be diffed or merged. This ships inside the MVP demo
%   download and lives in version control, so a single self-contained .m with
%   no binary companion is worth more than the layout convenience. It is also
%   the construction style that works unchanged from R2011a through current
%   releases -- App Designer needs R2016a and would not run on the licence
%   this project actually targets.
%
%   IT DOES NOT MODIFY wtRunSimulation
%   ----------------------------------
%   wtRunSimulation opens its own figure and draws its own plots. Rather than
%   add a "no plot" flag to a file that has been confirmed working, this
%   suppresses figure visibility around the call, deletes whatever figure the
%   call created, and redraws from the returned struct into its own axes. The
%   simulation code is left exactly as verified. If a doPlot argument is ever
%   added to wtRunSimulation, the suppression block below can be deleted.
%
%   R2011a COMPATIBILITY
%   --------------------
%   uicontrol/uipanel/axes, guidata-free nested-function callbacks, and
%   explicit normalized axes positions -- all available in R2011a and stable
%   since. Deliberately avoided: uitab/uitabgroup (undocumented before
%   R2014b), uifigure and App Designer (R2016a), SETDIFF on graphics handles
%   (unreliable once handles became objects in R2014b), and uigridlayout.
%
%   See also WTRUNSIMULATION, WTEXPORTSIMSAMPLES, WTBUILDMODEL.

%% ------------------------------------------------------------------------
%  Shared state (visible to every nested callback below)
%  ------------------------------------------------------------------------
out       = [];                      % last simulation result
ax        = zeros(1,6);              % the six plot axes
scenarios = {'ramp', 'step', 'turbulent', 'gust'};

% Guidance shown under the selector. The gust note is the one that matters:
% it is counter-intuitive and it decides whether an export is worth anything.
blurb = { ...
 ['RAMP  (recommended for export)' 10 10 ...
  'Wind sweeps 4 to 22 m/s, so the rotor accelerates through region 2' 10 ...
  'and then flattens as pitch control takes over at rated. On screen' 10 ...
  'that reads as a rotor spinning up and then holding.'], ...
 ['STEP' 10 10 ...
  'Wind jumps 8 to 16 m/s at the halfway point. The cleanest single' 10 ...
  'legible event; good when you want one transient and nothing else.'], ...
 ['TURBULENT' 10 10 ...
  'Mean 12 m/s with filtered noise at 16% intensity. Shakes the' 10 ...
  'controller. Not a Kaimal field -- not valid for fatigue work.'], ...
 ['GUST  (poor choice for export -- see note)' 10 10 ...
  'IEC extreme operating gust at rated wind: the best pitch-controller' 10 ...
  'stress test there is. But holding rotor speed CONSTANT is exactly' 10 ...
  'what the controller is for, so the exported rotor motion comes out' 10 ...
  'nearly flat. Best for the PITCH channel, worst for the rotor.'] };

%% ------------------------------------------------------------------------
%  Window
%  ------------------------------------------------------------------------
fig = figure( ...
    'Name',        'Dream World Maker  -  3 MW Turbine', ...
    'NumberTitle', 'off', ...
    'Units',       'pixels', ...
    'Position',    [80 60 1180 760], ...
    'Color',       get(0, 'DefaultUicontrolBackgroundColor'), ...
    'MenuBar',     'figure', ...
    'Toolbar',     'figure');

% ---- left column: controls -----------------------------------------------
ctrl = uipanel('Parent', fig, 'Title', 'Run', ...
    'Units', 'normalized', 'Position', [0.012 0.50 0.235 0.48]);

uicontrol('Parent', ctrl, 'Style', 'text', ...
    'Units', 'normalized', 'Position', [0.06 0.885 0.60 0.075], ...
    'String', 'Scenario', 'HorizontalAlignment', 'left', 'FontWeight', 'bold');

hScenario = uicontrol('Parent', ctrl, 'Style', 'popupmenu', ...
    'Units', 'normalized', 'Position', [0.06 0.80 0.86 0.075], ...
    'String', scenarios, ...
    'Value', 1, ...                                   % RAMP is the default
    'Callback', @onScenarioChanged);

hBlurb = uicontrol('Parent', ctrl, 'Style', 'text', ...
    'Units', 'normalized', 'Position', [0.06 0.26 0.88 0.50], ...
    'String', blurb{1}, ...
    'HorizontalAlignment', 'left', 'FontSize', 8);

hRun = uicontrol('Parent', ctrl, 'Style', 'pushbutton', ...
    'Units', 'normalized', 'Position', [0.06 0.10 0.86 0.13], ...
    'String', 'Run simulation', 'FontWeight', 'bold', ...
    'Callback', @onRun);

% ---- left column: export -------------------------------------------------
expp = uipanel('Parent', fig, 'Title', 'Export to DWM world package', ...
    'Units', 'normalized', 'Position', [0.012 0.175 0.235 0.30]);

uicontrol('Parent', expp, 'Style', 'text', ...
    'Units', 'normalized', 'Position', [0.06 0.79 0.45 0.13], ...
    'String', 'Sample rate', 'HorizontalAlignment', 'left');
hRate = uicontrol('Parent', expp, 'Style', 'edit', ...
    'Units', 'normalized', 'Position', [0.52 0.80 0.24 0.14], ...
    'String', '30', 'BackgroundColor', 'w');
uicontrol('Parent', expp, 'Style', 'text', ...
    'Units', 'normalized', 'Position', [0.78 0.79 0.18 0.13], ...
    'String', 'Hz', 'HorizontalAlignment', 'left');

uicontrol('Parent', expp, 'Style', 'text', ...
    'Units', 'normalized', 'Position', [0.06 0.60 0.45 0.13], ...
    'String', 'Base name', 'HorizontalAlignment', 'left');
hBase = uicontrol('Parent', expp, 'Style', 'edit', ...
    'Units', 'normalized', 'Position', [0.06 0.42 0.88 0.16], ...
    'String', 'wtSimSamples.csv', 'BackgroundColor', 'w', ...
    'HorizontalAlignment', 'left');

hExport = uicontrol('Parent', expp, 'Style', 'pushbutton', ...
    'Units', 'normalized', 'Position', [0.06 0.10 0.88 0.24], ...
    'String', 'Write channel CSVs', ...
    'Enable', 'off', ...                              % nothing to export yet
    'Callback', @onExport);

% ---- left column: status -------------------------------------------------
stat = uipanel('Parent', fig, 'Title', 'Status', ...
    'Units', 'normalized', 'Position', [0.012 0.012 0.235 0.15]);
hStatus = uicontrol('Parent', stat, 'Style', 'text', ...
    'Units', 'normalized', 'Position', [0.05 0.05 0.90 0.88], ...
    'String', 'Ready. Press Run.', ...
    'HorizontalAlignment', 'left', 'FontSize', 8);

% ---- right: plots --------------------------------------------------------
plotp = uipanel('Parent', fig, 'Title', 'Results', ...
    'Units', 'normalized', 'Position', [0.258 0.012 0.732 0.968]);

% Axes placed by hand rather than with SUBPLOT: subplot cannot be given a
% uipanel parent in R2011a, so explicit normalized positions are the portable
% way to get a 3x2 grid inside a panel.
col = [0.085 0.575];
row = [0.695 0.395 0.085];
w   = 0.375;  h = 0.225;
k   = 0;
for r = 1:3
    for c = 1:2
        k = k + 1;
        ax(k) = axes('Parent', plotp, 'Units', 'normalized', ...
                     'Position', [col(c) row(r) w h], 'FontSize', 8); %#ok<LAXES>
    end
end
resetAxes();

%% ========================================================================
%  Callbacks
%  ========================================================================

    function onScenarioChanged(src, ~) %#ok<INUSD>
        set(hBlurb, 'String', blurb{get(hScenario, 'Value')});
    end

% ------------------------------------------------------------------------
    function onRun(~, ~)
        name = scenarios{get(hScenario, 'Value')};
        setStatus(sprintf('Running "%s" ...', name));
        set([hRun hExport], 'Enable', 'off');
        drawnow;

        try
            out = runQuietly(name);
        catch err
            out = [];
            set(hRun, 'Enable', 'on');
            resetAxes();
            setStatus(['FAILED: ' err.message]);
            return
        end

        drawPlots(name);
        set(hRun, 'Enable', 'on');
        set(hExport, 'Enable', 'on');
        setStatus(summaryText());
    end

% ------------------------------------------------------------------------
    function onExport(~, ~)
        if isempty(out)
            setStatus('Nothing to export -- run a simulation first.');
            return
        end

        rate = str2double(get(hRate, 'String'));
        if isnan(rate) || rate <= 0
            setStatus('Sample rate must be a positive number.');
            return
        end
        base = strtrim(get(hBase, 'String'));
        if isempty(base)
            setStatus('Base name cannot be empty.');
            return
        end

        setStatus('Writing CSVs ...');
        drawnow;
        try
            % wtExportSimSamples prints its own report to the console and
            % raises its steady-state warning there; the status line gets the
            % short version.
            files = wtExportSimSamples(out, base, rate);
        catch err
            setStatus(['EXPORT FAILED: ' err.message]);
            return
        end

        setStatus(sprintf('Wrote %d channel file(s).\nSee the console for paths\nand the steady-state check.', ...
                          numel(files)));
    end

%% ========================================================================
%  Helpers
%  ========================================================================

    function res = runQuietly(name)
        %RUNQUIETLY  Call wtRunSimulation without letting its figure appear.
        %   Records the figures that exist beforehand, hides new ones while
        %   the call runs, then deletes whatever it created. Comparison is by
        %   an explicit loop rather than SETDIFF, which is unreliable on
        %   graphics handles once they became objects in R2014b.
        before = findobj(0, 'Type', 'figure');
        oldVis = get(0, 'DefaultFigureVisible');
        set(0, 'DefaultFigureVisible', 'off');
        try
            res = wtRunSimulation(name);
        catch err
            set(0, 'DefaultFigureVisible', oldVis);
            killNew(before);
            rethrow(err);
        end
        set(0, 'DefaultFigureVisible', oldVis);
        killNew(before);
    end

    function killNew(before)
        after = findobj(0, 'Type', 'figure');
        for hh = after(:)'
            if hh ~= fig && ~any(before == hh)
                delete(hh);
            end
        end
    end

% ------------------------------------------------------------------------
    function drawPlots(name)
        P  = out.P;
        tt = out.t;

        plotOne(ax(1), tt, out.v_wind,            'wind [m/s]',        'Wind speed',              P.v_rated);
        plotOne(ax(2), tt, out.P_elec/1e6,        'P_{elec} [MW]',     'Electrical power',        P.P_rated/1e6);
        plotOne(ax(3), tt, out.omega_r*60/(2*pi), '\omega_r [rpm]',    'Rotor speed',             P.omega_rMax*60/(2*pi));
        plotOne(ax(4), tt, out.beta*180/pi,       '\beta [deg]',       'Collective pitch',        []);
        plotOne(ax(5), tt, out.Cp,                'C_p [-]',           'Power coefficient',       P.Cp_max);
        plotOne(ax(6), tt, out.x_t*1000,          'x_{tower} [mm]',    'Tower fore-aft',          []);

        xlabel(ax(5), 'time [s]');
        xlabel(ax(6), 'time [s]');
        set(get(ax(1), 'Title'), 'String', sprintf('Wind speed  (%s)', name));
    end

    function plotOne(a, x, y, ylab, ttl, refLine)
        cla(a);
        plot(a, x, y, 'LineWidth', 1.1);
        grid(a, 'on');
        ylabel(a, ylab);
        title(a, ttl);
        if ~isempty(refLine)
            % Stands in for YLINE, which needs R2018b.
            xl = get(a, 'XLim');
            washHeld = ishold(a);
            hold(a, 'on');
            plot(a, xl, [refLine refLine], 'k--', 'LineWidth', 0.75);
            if ~washHeld, hold(a, 'off'); end
        end
        set(a, 'FontSize', 8);
    end

    function resetAxes()
        for a = ax
            cla(a); grid(a, 'on'); set(a, 'FontSize', 8);
        end
        title(ax(1), 'Wind speed');       title(ax(2), 'Electrical power');
        title(ax(3), 'Rotor speed');      title(ax(4), 'Collective pitch');
        title(ax(5), 'Power coefficient');title(ax(6), 'Tower fore-aft');
    end

% ------------------------------------------------------------------------
    function s = summaryText()
        %SUMMARYTEXT  Post-run checks plus the figure that decides whether an
        %   export is worth making. The checks mirror wtRunSimulation's own.
        P = out.P;

        % Same four checks wtRunSimulation prints to the console. Kept as a
        % plain vector rather than a helper: a nested function inside a nested
        % function is legal but needlessly clever for counting to four.
        ok = [ max(out.P_elec)       <= 1.10*P.P_rated
               max(out.omega_g)      <= P.sup.overspeedTrip
               max(out.Cp)           <= 0.593
               max(out.beta)*180/pi  <= 90.001 ];
        pass = sum(ok);
        fail = numel(ok) - pass;

        om   = out.omega_r;
        mn   = mean(om);
        if mn > eps, variation = (max(om)-min(om))/mn; else variation = 0; end

        s = sprintf('Checks: %d pass, %d fail\nRotor speed varies %.1f%%', ...
                    pass, fail, variation*100);
        if variation < 0.02
            s = [s sprintf('\nSTEADY STATE - exported motion\nwill look like a constant rate.')];
        end
    end

% ------------------------------------------------------------------------
    function setStatus(msg)
        set(hStatus, 'String', msg);
        drawnow;
    end

end
