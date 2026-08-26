function h = wtAdd(src, dst, pos, varargin)
%WTADD  Add a Simulink block at a given position, robustly across releases.
%
%   H = WTADD(SRC, DST, POS, 'Param', value, ...) adds block SRC at path DST,
%   positions it, and applies any further name/value parameters.
%
%   SRC   library path, e.g. 'simulink/Math Operations/Gain'
%   DST   full destination path, e.g. 'myModel/Drivetrain/RotorInertia'
%   POS   [x y] top-left corner, or [x1 y1 x2 y2] full rectangle
%
%   WHY THIS DOES NOT JUST CALL ADD_BLOCK
%   ------------------------------------
%   Library paths like 'simulink/Sources/In1' are DISPLAY paths. They depend
%   on the Simulink library being loaded, and on the block's display name in
%   that release -- and those names have changed (In1, Lookup Table (n-D),
%   and others). On R2011a this fails outright with "There is no block named
%   'simulink/Sources/In1'".
%
%   Every block also has a stable BlockType reachable as 'built-in/<Type>',
%   which has not changed across releases and needs no library loaded. So
%   each library path is tried first (keeping the builder files readable),
%   and on failure the built-in equivalent is used instead.
%
%   Adding a block that already exists is an error rather than a silent
%   replace -- a duplicate name almost always means a copy-paste slip.
%
%   See also WTLINE, WTADDLOOKUP2D, WTBUILDMODEL.

if numel(pos) == 2
    pos = [pos(1), pos(2), pos(1)+60, pos(2)+40];
end

% --- refuse to overwrite ------------------------------------------------
% get_param rather than getSimulinkBlockHandle: the latter does not exist on
% older releases. It throws for a path that is not there, which is the test.
blockExists = true;
try
    get_param(dst, 'Handle');
catch
    blockExists = false;
end
if blockExists
    error('wtAdd:blockExists', 'Block already exists: %s', dst);
end

% --- place the block ----------------------------------------------------
h = [];
placed = false;
firstErr = '';

try
    h = add_block(src, dst, 'Position', pos);
    placed = true;
catch ME
    firstErr = ME.message;
end

if ~placed
    fallback = wtBuiltInFor(src);
    if isempty(fallback)
        error('wtAdd:noSuchBlock', ...
            ['Could not add "%s" at %s, and no built-in fallback is known ' ...
             'for it.\nOriginal error: %s\n' ...
             'Add an entry to the map in wtAdd/wtBuiltInFor.'], ...
            src, dst, firstErr);
    end
    try
        h = add_block(fallback, dst, 'Position', pos);
    catch ME2
        error('wtAdd:noSuchBlock', ...
            ['Could not add "%s" at %s.\n' ...
             '  library path failed : %s\n' ...
             '  built-in "%s" failed: %s'], ...
            src, dst, firstErr, fallback, ME2.message);
    end
end

if ~isempty(varargin)
    set_param(dst, varargin{:});
end

end


% -------------------------------------------------------------------------
function bi = wtBuiltInFor(src)
%WTBUILTINFOR  Map a Simulink library display path to its built-in BlockType.
%
%   Returns '' when no mapping is known.
%
%   The right-hand side is the block's BlockType property, which is what
%   'built-in/...' resolves against. These names are stable across releases;
%   the left-hand display names are not.

map = { ...
    'simulink/Sources/In1',                                  'built-in/Inport'
    'simulink/Sinks/Out1',                                   'built-in/Outport'
    'simulink/Sources/Constant',                             'built-in/Constant'
    'simulink/Sources/From Workspace',                       'built-in/FromWorkspace'
    'simulink/Sinks/To Workspace',                           'built-in/ToWorkspace'
    'simulink/Math Operations/Gain',                         'built-in/Gain'
    'simulink/Math Operations/Sum',                          'built-in/Sum'
    'simulink/Math Operations/Product',                      'built-in/Product'
    'simulink/Math Operations/Math Function',                'built-in/Math'
    'simulink/Math Operations/MinMax',                       'built-in/MinMax'
    'simulink/Math Operations/Trigonometric Function',       'built-in/Trigonometry'
    'simulink/Continuous/Integrator',                        'built-in/Integrator'
    'simulink/Continuous/Transfer Fcn',                      'built-in/TransferFcn'
    'simulink/Continuous/Derivative',                        'built-in/Derivative'
    'simulink/Sources/Step',                                 'built-in/Step'
    'simulink/Discontinuities/Saturation',                   'built-in/Saturate'
    'simulink/Discontinuities/Relay',                        'built-in/Relay'
    'simulink/Signal Routing/Switch',                        'built-in/Switch'
    'simulink/Discontinuities/Rate Limiter',                 'built-in/RateLimiter'
    'simulink/Discontinuities/Dead Zone',                    'built-in/DeadZone'
    'simulink/Logic and Bit Operations/Relational Operator', 'built-in/RelationalOperator'
    'simulink/Signal Routing/Mux',                           'built-in/Mux'
    'simulink/Signal Routing/Demux',                         'built-in/Demux'
    'simulink/Ports & Subsystems/Subsystem',                 'built-in/SubSystem'};

idx = find(strcmp(map(:,1), src), 1);
if isempty(idx)
    bi = '';
else
    bi = map{idx, 2};
end

end
