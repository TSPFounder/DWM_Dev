function wtCreateRequirementLinks(modelName, bomPath, wordPath)
%WTCREATEREQUIREMENTLINKS  Link the turbine model's subsystems to the BOM via RMI.
%
%   WTCREATEREQUIREMENTLINKS() links every subsystem of wtTurbine3MW to its
%   rows in Wind_Turbine_BOM.xlsx, using the Requirements Management Interface
%   from Simulink Verification and Validation.
%
%   WTCREATEREQUIREMENTLINKS(MODEL, BOMPATH, WORDPATH) overrides the defaults.
%   WORDPATH is optional -- pass '' to link only to the spreadsheet.
%
%   Requires: Simulink Verification and Validation, Windows, and Excel/Word
%   installed if you want navigation to actually open the documents.
%
%   ACTIVEX IS NOT NEEDED FOR ANY OF THIS
%   -------------------------------------
%   Creating links writes data into the MODEL, not into the Office file, so it
%   is pure MATLAB. Navigating Simulink -> document drives the installed Office
%   application through COM, which needs Office present but no control
%   registration. The only thing that needs a registered ActiveX control is a
%   BACKLINK -- a button embedded in the Word/Excel file that jumps back into
%   the model. This function does not create those, so nothing here is gated on
%   ActiveX. Add backlinks later with rmi('insertRefs', ...) if you want the
%   reverse direction, and register the control then.
%
%   RE-RUNNING IS SAFE
%   ------------------
%   rmi('set', ...) REPLACES the link set on each object rather than appending,
%   and every object's links are built as one array below. So running this
%   twice produces the same result as running it once -- no duplicates.
%
%   See also WTBUILDMODEL, WTRUNSIMULATION, RMI.

%% ------------------------------------------------------------------------
%  Defaults
%  ------------------------------------------------------------------------
if nargin < 1 || isempty(modelName), modelName = 'wtTurbine3MW';           end
if nargin < 2 || isempty(bomPath),   bomPath   = 'Wind_Turbine_BOM.xlsx';  end
if nargin < 3,                       wordPath  = '';                       end

%% ------------------------------------------------------------------------
%  Preconditions -- check these before touching the model
%  ------------------------------------------------------------------------
% RMI ships with Simulink Verification and Validation, which is separately
% licensed. Checking up front gives a clear message instead of an obscure
% "Undefined function 'rmi'" three steps later.
if isempty(ver('slvnv'))
    error('wtCreateRequirementLinks:noVnV', ...
        ['Simulink Verification and Validation is not installed. RMI ships ' ...
         'with that product; run "ver" to confirm what you have.']);
end

% Absolute path: RMI stores whatever string it is given. A relative path
% resolves against the current directory at NAVIGATION time, not creation
% time, so it silently breaks the moment someone cd's elsewhere.
bomFull = wtResolveDoc(bomPath);
if isempty(bomFull)
    error('wtCreateRequirementLinks:noBom', ...
        ['Cannot find the BOM at "%s". Pass the full path as the second ' ...
         'argument, or put the file on the MATLAB path.'], bomPath);
end

wordFull = '';
if ~isempty(wordPath)
    wordFull = wtResolveDoc(wordPath);
    if isempty(wordFull)
        warning('wtCreateRequirementLinks:noWord', ...
            'Cannot find the Word document at "%s" -- skipping those links.', ...
            wordPath);
    end
end

if ~wtModelExists(modelName)
    error('wtCreateRequirementLinks:noModel', ...
        ['Model "%s" not found. Build it first:\n' ...
         '    wtBuildModel(''%s'');'], modelName, modelName);
end

load_system(modelName);

%% ------------------------------------------------------------------------
%  The link table
%  ------------------------------------------------------------------------
%  Columns: { subsystem, location id, description }
%
%  The location id tells RMI where in the document to land:
%      '?text'   search for this text
%      '@name'   named item (a Word bookmark, or an Excel named range)
%      '#n'      page or item number
%
%  Search text is used here because the BOM's Part No column already holds
%  these values and needs no preparation. If a part number ever matches more
%  than one cell, switch that row to a named range in the workbook and use
%  '@Name' instead -- more robust, but it requires editing the .xlsx first.
%
%  Every one of these part numbers already appears in the corresponding
%  builder function's header comment, so this formalises traceability that is
%  currently only prose.

L = { ...
    'RotorAero',     '?1100', 'BOM 1100 - Blade (58.5 m, lofted aerofoil)'
    'RotorAero',     '?1110', 'BOM 1110 - Spar caps'
    'RotorAero',     '?1200', 'BOM 1200 - Hub'

    'Drivetrain',    '?2100', 'BOM 2100 - Main shaft (K_shaft derived from this)'
    'Drivetrain',    '?2200', 'BOM 2200 - Main bearings'
    'Drivetrain',    '?2300', 'BOM 2300 - Gearbox (N = 104.3)'
    'Drivetrain',    '?2400', 'BOM 2400 - High-speed coupling'

    'PitchActuator', '?1300', 'BOM 1300 - Pitch bearing'
    'PitchActuator', '?1310', 'BOM 1310 - Pitch drive (8 deg/s rate limit)'
    'PitchActuator', '?1320', 'BOM 1320 - Pitch backup energy store'

    'GenConverter',  '?2600', 'BOM 2600 - Generator'
    'GenConverter',  '?6100', 'BOM 6100 - Power converter'

    'TowerDynamics', '?4100', 'BOM 4100 - Tower section, bottom'
    'TowerDynamics', '?4130', 'BOM 4130 - Tower flange bolting'

    'YawSystem',     '?3100', 'BOM 3100 - Yaw bearing'
    'YawSystem',     '?3200', 'BOM 3200 - Yaw drives (0.5 deg/s)'

    'Controller',    '?7100', 'BOM 7100 - Main turbine controller'
    'Controller',    '?7210', 'BOM 7210 - Anemometer'
    'Controller',    '?7240', 'BOM 7240 - Generator speed encoder'
    'Controller',    '?7400', 'BOM 7400 - Safety chain (NOT modelled here)'
    };

%% ------------------------------------------------------------------------
%  Build and apply
%  ------------------------------------------------------------------------
subsystems = unique(L(:,1));
applied = 0;
skipped = {};

for k = 1:numel(subsystems)
    sub  = subsystems{k};
    path = [modelName '/' sub];

    if ~wtBlockExists(path)
        skipped{end+1} = sub; %#ok<AGROW>
        continue
    end

    rows = find(strcmp(L(:,1), sub));

    % Build the whole array first, then set once. Setting per-link would
    % replace the previous one each time and leave only the last.
    links = [];
    for r = rows(:)'
        lk = rmi('createEmpty');
        lk.description = L{r, 3};
        lk.doc         = bomFull;
        lk.id          = L{r, 2};
        lk.linked      = true;
        if isempty(links)
            links = lk;
        else
            links(end+1) = lk; %#ok<AGROW>
        end
    end

    % Optional Word links: one per subsystem, pointing at a same-named
    % heading. Requires the document to actually contain those headings.
    if ~isempty(wordFull)
        lk = rmi('createEmpty');
        lk.description = sprintf('Design description - %s', sub);
        lk.doc         = wordFull;
        lk.id          = ['?' sub];
        lk.linked      = true;
        links(end+1) = lk; %#ok<AGROW>
    end

    rmi('set', path, links);
    applied = applied + numel(links);
    fprintf('  %-16s %d link(s)\n', sub, numel(links));
end

%% ------------------------------------------------------------------------
%  Save and report
%  ------------------------------------------------------------------------
save_system(modelName);

fprintf('\n%d link(s) applied across %d subsystem(s).\n', ...
    applied, numel(subsystems) - numel(skipped));
fprintf('Document: %s\n', bomFull);
if ~isempty(skipped)
    fprintf('SKIPPED (not found in the model): %s\n', wtJoin(skipped, ', '));
    fprintf('  Was the model built by the current wtBuildModel?\n');
end

fprintf('\nTo view:  right-click a subsystem -> Requirements\n');
fprintf('To audit: rmi(''report'', ''%s'')\n', modelName);
fprintf('To clear: rmi(''clearAll'', ''%s'')\n', modelName);

end


% =========================================================================
% Helpers -- each written to work on R2011a, where several of the obvious
% modern functions do not exist.
% =========================================================================

function full = wtResolveDoc(p)
%WTRESOLVEDOC  Absolute path to a document, or '' if it cannot be found.
full = '';
if exist(p, 'file') == 2
    w = which(p);
    if ~isempty(w)
        full = w;                 % found on the path
    else
        d = dir(p);               % a relative or absolute path that exists
        if ~isempty(d)
            full = fullfile(wtAbsDir(p), d(1).name);
        end
    end
end
end


function d = wtAbsDir(p)
%WTABSDIR  Absolute directory containing p.
[pathPart, ~, ~] = fileparts(p);
if isempty(pathPart)
    d = pwd;
else
    here = pwd;
    try
        cd(pathPart);
        d = pwd;
        cd(here);
    catch
        cd(here);
        d = pathPart;
    end
end
end


function tf = wtModelExists(modelName)
%WTMODELEXISTS  True if the model is loaded or findable on disk.
%   Checks both extensions: R2011a writes .mdl, later releases .slx.
tf = any(strcmp(find_system('type', 'block_diagram'), modelName)) ...
     || (exist([modelName '.mdl'], 'file') == 4) ...
     || (exist([modelName '.mdl'], 'file') == 2) ...
     || (exist([modelName '.slx'], 'file') == 4) ...
     || (exist([modelName '.slx'], 'file') == 2);
end


function tf = wtBlockExists(path)
%WTBLOCKEXISTS  True if a block exists at the given path.
%   get_param rather than getSimulinkBlockHandle, which post-dates R2011a.
tf = true;
try
    get_param(path, 'Handle');
catch
    tf = false;
end
end


function s = wtJoin(parts, sep)
%WTJOIN  Join a cellstr with a separator.
%   Stands in for STRJOIN, which was introduced in R2013a and so does not
%   exist on the R2011a target.
s = '';
for i = 1:numel(parts)
    if i > 1
        s = [s sep]; %#ok<AGROW>
    end
    s = [s parts{i}]; %#ok<AGROW>
end
end
