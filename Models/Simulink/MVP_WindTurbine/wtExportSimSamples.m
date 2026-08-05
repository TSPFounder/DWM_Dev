function files = wtExportSimSamples(out, csvPath, sampleRate)
%WTEXPORTSIMSAMPLES  Write turbine motion channels to DWM world-package CSVs.
%
%   WTEXPORTSIMSAMPLES(OUT) writes 'wtSimSamples_*.csv' in the current
%   directory from the OUT struct returned by WTRUNSIMULATION.
%
%   WTEXPORTSIMSAMPLES(OUT, CSVPATH) uses CSVPATH as the base name; the
%   channel suffix is inserted before the extension, so 'a/b/sim.csv' gives
%   'a/b/sim_rotor.csv', 'a/b/sim_pitch.csv' and so on.
%
%   WTEXPORTSIMSAMPLES(OUT, CSVPATH, SAMPLERATE) resamples at SAMPLERATE Hz
%   (default 30).
%
%   FILES = WTEXPORTSIMSAMPLES(...) returns the paths written.
%
%   ONE FILE PER BLOCK, WHICH IS WHY THERE ARE SEVERAL
%   --------------------------------------------------
%   The world package's SimSamples table is keyed on (BlockId, Time), so a
%   mechanism with several moving parts is expressed as several BLOCKS, not
%   as extra columns. The pendulum tracer used one block because a pendulum
%   has one moving part. A turbine has four, plus signals worth putting on a
%   HUD, so this writes one CSV per block and WriteTurbine loads them into
%   one package. NO SCHEMA CHANGE IS INVOLVED -- this is the schema being
%   used as designed.
%
%   Each file keeps the exact three-column format the C# reader already
%   parses, so LoadSamplesFromCsv needs no modification:
%
%       Time,Position,Velocity
%
%   What those two data columns MEAN is per-block, and is listed below.
%
%   CHANNELS WRITTEN
%   ----------------
%     rotor   Position = azimuth [rad, UNWRAPPED]   Velocity = omega_r [rad/s]
%     pitch   Position = beta [rad]                 Velocity = beta rate [rad/s]
%     yaw     Position = yaw ERROR [rad]            Velocity = yaw rate [rad/s]
%     tower   Position = fore-aft x_t [m]           Velocity = x_t rate [m/s]
%     power   Position = P_elec [W]                 Velocity = v_wind [m/s]
%
%   The first four drive geometry: the rotor turns, the blades feather, the
%   nacelle slews, the tower flexes. The fifth is not kinematic at all --
%   it rides in the same table because Blocks carries a BlockType, so a
%   'Signal' block can use Position and Velocity as two generic channel
%   slots. That is a deliberate reuse, not an abuse; it is written down in
%   WriteTurbine so nobody has to guess.
%
%   YAW IS AN *ERROR*, NOT AN ABSOLUTE HEADING -- READ THIS BEFORE USING IT
%   ----------------------------------------------------------------------
%   The model logs yaw ERROR: the angle between where the nacelle points and
%   where the wind is coming from. It is NOT the nacelle's heading in world
%   space, and driving a nacelle's yaw directly from it will look wrong.
%   Absolute heading = wind direction - yaw error, and WIND DIRECTION IS NOT
%   IN THE LOG. To get it, add windDir as a 13th channel on the LogMux in
%   wtBuildModel and unpack it in wtRunSimulation. Until then, treat this
%   channel as diagnostic rather than as animation input.
%
%   AZIMUTH IS UNWRAPPED AND GROWS WITHOUT BOUND
%   --------------------------------------------
%   Not wrapped to [0, 2*pi) deliberately: wrapping introduces a
%   discontinuity every revolution, and anything interpolating between two
%   samples that straddle it produces a full backwards spin. The UE actor
%   takes the modulus at read time, where it cannot be interpolated across.
%
%   WHY RESAMPLE ONTO A UNIFORM GRID
%   --------------------------------
%   A variable-step solver can emit the SAME TIMESTAMP TWICE around events.
%   SimSamples is keyed on (BlockId, Time), so a duplicate is not cosmetic --
%   the INSERT fails and the export dies partway through. Resampling also
%   cuts thousands of solver points down to something proportionate.
%
%   R2011a COMPATIBILITY
%   --------------------
%   Everything here predates R2011a: CUMTRAPZ, INTERP1, UNIQUE, FILEPARTS,
%   FULLFILE, FOPEN/FPRINTF. No WRITETABLE (R2013b), no STRING (R2016b), no
%   STRJOIN (R2013a). The MVP turbine depends on this running under the 2011a
%   licence -- see SCOPE.md 2026-08-02. A change that works on a modern
%   release proves nothing here.
%
%   See also WTRUNSIMULATION, WTBUILDMODEL.

%% ------------------------------------------------------------------------
%  Defaults and validation
%  ------------------------------------------------------------------------
if nargin < 2 || isempty(csvPath),    csvPath    = 'wtSimSamples.csv'; end
if nargin < 3 || isempty(sampleRate), sampleRate = 30;                 end

if ~isstruct(out) || ~isfield(out, 't') || ~isfield(out, 'omega_r')
    error('wtExportSimSamples:badInput', ...
        ['First argument must be the struct returned by wtRunSimulation ' ...
         '(needs at least the t and omega_r fields).']);
end

t = out.t(:);
if numel(t) < 2
    error('wtExportSimSamples:tooShort', ...
        'Need at least two samples; the log has %d.', numel(t));
end

%% ------------------------------------------------------------------------
%  Strip duplicate timestamps before anything else
%  ------------------------------------------------------------------------
% Repeated times at solver events break CUMTRAPZ (zero-width panel) and
% INTERP1 (non-monotonic sample points), and INTERP1's error does not make
% the cause obvious.
[t, keep] = unique(t, 'first');
[t, order] = sort(t);
keep = keep(order);

%% ------------------------------------------------------------------------
%  Uniform grid
%  ------------------------------------------------------------------------
dt    = 1 / sampleRate;
tGrid = (t(1) : dt : t(end))';

% Guarantee the final point survives; a run whose length is not an exact
% multiple of dt would otherwise lose up to one sample period off the end,
% which matters when the last thing that happens is the thing worth showing.
if tGrid(end) < t(end) - 1e-9
    tGrid(end+1) = t(end); %#ok<AGROW>
end

%% ------------------------------------------------------------------------
%  Build the channel table
%  ------------------------------------------------------------------------
% Rotor azimuth is INTEGRATED from omega_r on the solver's own points,
% before resampling. The model has no integrator on rotor angle because
% nothing in the control loop needs it, and integrating after resampling
% would discard exactly the detail that makes exporting a transient useful.
omega   = pick(out, 'omega_r', keep);
azimuth = cumtrapz(t, omega);

chan = {};   % {suffix, position vector (on t), velocity vector or [] to differentiate}
chan(end+1,:) = {'rotor', azimuth,                     omega};
chan(end+1,:) = {'pitch', pick(out, 'beta',     keep), []};
chan(end+1,:) = {'yaw',   pick(out, 'yawError', keep), []};
chan(end+1,:) = {'tower', pick(out, 'x_t',      keep), []};

% Signal block: not kinematic. Position and Velocity are used as two plain
% channel slots, which the Blocks row marks with BlockType 'Signal'.
pElec = pick(out, 'P_elec', keep);
vWind = pick(out, 'v_wind', keep);
if isempty(vWind), vWind = pick(out, 'v_rel', keep); end
chan(end+1,:) = {'power', pElec, vWind};

%% ------------------------------------------------------------------------
%  Write
%  ------------------------------------------------------------------------
[baseDir, baseName, baseExt] = fileparts(csvPath);
if isempty(baseExt), baseExt = '.csv'; end

files = {};
fprintf('\n=== Exported sim samples ===\n');
fprintf('  Window   : %.3f to %.3f s\n', t(1), t(end));
fprintf('  Samples  : %d per channel at %g Hz\n\n', numel(tGrid), sampleRate);

for k = 1:size(chan, 1)
    suffix = chan{k,1};
    pos    = chan{k,2};
    vel    = chan{k,3};

    if isempty(pos)
        fprintf('  %-6s SKIPPED - channel not present in the log\n', suffix);
        continue
    end

    posGrid = interp1(t, pos, tGrid, 'linear');

    if isempty(vel)
        % Uniform grid, so a plain difference is exact enough and needs no
        % GRADIENT (whose two-argument form has varied across releases).
        velGrid = zeros(size(posGrid));
        if numel(posGrid) > 1
            velGrid(1:end-1) = diff(posGrid) / dt;
            velGrid(end)     = velGrid(end-1);
        end
    else
        velGrid = interp1(t, vel, tGrid, 'linear');
    end

    fname = fullfile(baseDir, [baseName '_' suffix baseExt]);
    writeChannel(fname, tGrid, posGrid, velGrid);
    files{end+1} = fname; %#ok<AGROW>

    fprintf('  %-6s %-42s  range % .4g to % .4g\n', ...
            suffix, fname, min(posGrid), max(posGrid));
end

%% ------------------------------------------------------------------------
%  Steady-state guard
%  ------------------------------------------------------------------------
% The point of exporting from a real model rather than generating constant
% rotation is the TRANSIENT. If the window is steady state the motion is
% indistinguishable from a for-loop, and describing it as simulation output
% would be true but hollow. Warn rather than error -- a steady-state export
% is a legitimate thing to want on purpose, just not by accident.
omGrid = interp1(t, omega, tGrid, 'linear');
span   = max(omGrid) - min(omGrid);
mn     = mean(omGrid);
if mn > eps, variation = span / mn; else variation = 0; end

fprintf('\n  Rotor speed varies %.1f%% across this window (%.4f to %.4f rad/s)\n', ...
        variation*100, min(omGrid), max(omGrid));
fprintf('  Azimuth travel  : %.2f rad (%.2f revolutions, UNWRAPPED)\n', ...
        max(cumtrapz(tGrid, omGrid)), max(cumtrapz(tGrid, omGrid))/(2*pi));

if variation < 0.02
    warning('wtExportSimSamples:steadyState', ...
        ['Rotor speed varies by only %.1f%% across this window. The exported ' ...
         'motion will look identical to a constant rotation rate, so nothing ' ...
         'about the model is visible in it. Use the "ramp" or "step" scenario ' ...
         'if the point is to show the physics -- note that "gust" holds rotor ' ...
         'speed nearly constant BY DESIGN, because that is what the pitch ' ...
         'controller is for.'], variation*100);
end

fprintf('\nNext: pass these to WorldPackageExporter.WriteTurbine.\n');

end


% =========================================================================
function v = pick(s, field, keep)
%PICK  Column vector for FIELD, de-duplicated by KEEP, or [] if absent.
if isfield(s, field)
    v = s.(field);
    v = v(:);
    v = v(keep);
else
    v = [];
end
end


% =========================================================================
function writeChannel(fname, t, pos, vel)
%WRITECHANNEL  Write one Time,Position,Velocity CSV.
%   FOPEN/FPRINTF rather than CSVWRITE because CSVWRITE cannot write the
%   header row. '%.9g' keeps the text round-trip lossless for these
%   magnitudes without emitting 17 digits of noise.
fid = fopen(fname, 'w');
if fid < 0
    error('wtExportSimSamples:cannotOpen', ...
        'Cannot open "%s" for writing. Check the path and permissions.', fname);
end
try
    fprintf(fid, 'Time,Position,Velocity\n');
    for k = 1:numel(t)
        fprintf(fid, '%.9g,%.9g,%.9g\n', t(k), pos(k), vel(k));
    end
catch writeErr
    fclose(fid);
    rethrow(writeErr);
end
fclose(fid);
end
