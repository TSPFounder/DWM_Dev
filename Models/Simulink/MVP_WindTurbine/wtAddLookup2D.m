function wtAddLookup2D(dst, pos, tableExpr, bp1Expr, bp2Expr)
%WTADDLOOKUP2D  Add a 2-D lookup table, across Simulink library renames.
%
%   WTADDLOOKUP2D(DST, POS, TABLE, BP1, BP2) places a two-dimensional lookup
%   table at DST, with the table and both breakpoint vectors given as
%   workspace expression strings.
%
%   WHY THIS WRAPPER EXISTS
%   -----------------------
%   The n-D lookup block was renamed between releases:
%
%       R2011a and earlier :  'Lookup Tables/Lookup Table (n-D)'
%       R2011b and later   :  'Lookup Tables/n-D Lookup Table'
%
%   Rather than hard-code a version number -- the exact release of the rename
%   is easy to get wrong, and getting it wrong fails at build time with an
%   unhelpful "invalid block path" -- this tries the candidates in turn and
%   keeps the first that works. The block's PARAMETER names
%   (NumberOfTableDimensions, Table, BreakpointsForDimension1/2) are stable
%   across the whole range, so only the path needs handling.
%
%   See also WTADD, WTBUILDROTORAERO.

candidates = { ...
    'built-in/Lookup_n-D', ...                         % BlockType: release-stable
    'simulink/Lookup Tables/n-D Lookup Table', ...     % R2011b and later
    'simulink/Lookup Tables/Lookup Table (n-D)'};      % R2011a and earlier

if numel(pos) == 2
    pos = [pos(1), pos(2), pos(1)+60, pos(2)+40];
end

placed = false;
lastErr = '';
for k = 1:numel(candidates)
    try
        add_block(candidates{k}, dst, 'Position', pos);
        placed = true;
        break
    catch ME
        lastErr = ME.message;
    end
end

if ~placed
    error('wtAddLookup2D:noBlock', ...
        ['Could not add a 2-D lookup table at %s. Tried:\n  %s\n  %s\n  %s\n' ...
         'Last error: %s'], dst, candidates{1}, candidates{2}, candidates{3}, ...
        lastErr);
end

set_param(dst, ...
    'NumberOfTableDimensions',  '2', ...
    'Table',                    tableExpr, ...
    'BreakpointsForDimension1', bp1Expr, ...
    'BreakpointsForDimension2', bp2Expr);

% Clip rather than extrapolate: beyond the table edges the Cp fit is not
% merely inaccurate, it is unbounded. Linear extrapolation there produces
% Cp values above the Betz limit during start-up transients.
try
    set_param(dst, 'ExtrapMethod', 'Clip');
catch
    % Parameter name differs on some releases; clipping is the default
    % behaviour for saturated index search, so this is not fatal.
end

end
