function wtLine(sys, connections)
%WTLINE  Connect a list of Simulink signal pairs inside one subsystem.
%
%   WTLINE(SYS, CONNECTIONS) draws every connection in the N-by-2 cell array
%   CONNECTIONS, where each row is {'SourceBlock/outport', 'DestBlock/inport'}.
%
%   Example:
%       wtLine(sys, { ...
%           'T_aero/1',      'SumRotor/1'
%           'ShaftTorque/1', 'SumRotor/2'});
%
%   Autorouting is on, so lines route around blocks instead of through them.
%
%   Each connection is attempted individually and a failure names the exact
%   pair that failed. A bare add_line error reports only the subsystem, which
%   in a model with ~200 signals is not enough to find the offending line.
%
%   See also WTADD, WTBUILDMODEL.

for k = 1:size(connections, 1)
    src = connections{k, 1};
    dst = connections{k, 2};
    try
        add_line(sys, src, dst, 'autorouting', 'on');
    catch ME
        error('wtLine:failed', ...
            'Could not connect "%s" -> "%s" in %s:\n  %s', ...
            src, dst, sys, ME.message);
    end
end

end
