function AT = wtGenerateAeroTables(P, nLambda, nBeta)
%WTGENERATEAEROTABLES  Pre-compute Cp and Ct surfaces for the Simulink lookup blocks.
%
%   AT = WTGENERATEAEROTABLES(P) returns a struct with breakpoint vectors and
%   the Cp / Ct tables evaluated over them.
%
%   AT.lambda   [1 x nLambda]  tip-speed ratio breakpoints
%   AT.beta     [1 x nBeta]    pitch angle breakpoints, RADIANS
%   AT.Cp       [nLambda x nBeta]
%   AT.Ct       [nLambda x nBeta]
%
%   WHY A LOOKUP TABLE RATHER THAN AN INLINE EQUATION
%   -------------------------------------------------
%   The Cp fit in WTCP could be embedded in a MATLAB Function block, but a
%   2-D lookup table is preferable here for three reasons: it is what a real
%   BEM-derived Cp surface would arrive as anyway (a table, not a formula),
%   it is far cheaper to evaluate inside a stiff solver loop, and it is
%   settable through plain SET_PARAM without touching the Stateflow API,
%   which keeps the model buildable on any Simulink installation.
%
%   To swap in real rotor data later, replace the body of this function with
%   a load() of the BEM output. Nothing downstream changes.
%
%   See also WTCP, WTBUILDROTORAERO.

if nargin < 1 || isempty(P),        P = wtParameters();  end
if nargin < 2 || isempty(nLambda),  nLambda = 61;        end
if nargin < 3 || isempty(nBeta),    nBeta   = 46;        end

% Lambda range: 0 to ~2x optimal covers start-up through overspeed.
AT.lambda = linspace(0.1, 18, nLambda);

% Pitch range: fine pitch to full feather.
AT.beta   = linspace(P.pitch.betaMin, P.pitch.betaMax, nBeta);

[L, B] = ndgrid(AT.lambda, AT.beta);
[AT.Cp, AT.Ct] = wtCp(L, B, P);

% ---- Consistency check against the declared design point ----------------
% If the fit's peak does not land near P.Cp_max / P.lambda_opt, then the
% Region II torque gain K_opt computed in wtParameters is inconsistent with
% the surface the model actually runs on, and the turbine will settle off
% its optimum. Better to fail loudly here than to explain a 2 % AEP shortfall
% later.
[cpPeak, idx]   = max(AT.Cp(:));
[iL, iB]        = ind2sub(size(AT.Cp), idx);
lambdaPeak      = AT.lambda(iL);
betaPeak        = AT.beta(iB);

AT.check = struct('Cp_peak', cpPeak, 'lambda_peak', lambdaPeak, ...
                  'beta_peak', betaPeak);

if abs(cpPeak - P.Cp_max) > 0.05
    warning('wtGenerateAeroTables:CpMismatch', ...
        ['Table peak Cp = %.3f but P.Cp_max = %.3f. K_opt in wtParameters ' ...
         'is derived from P.Cp_max, so Region II will not track the true ' ...
         'optimum. Reconcile the two before trusting power capture results.'], ...
        cpPeak, P.Cp_max);
end

if abs(lambdaPeak - P.lambda_opt) > 0.75
    warning('wtGenerateAeroTables:LambdaMismatch', ...
        ['Table peak occurs at lambda = %.2f but P.lambda_opt = %.2f. ' ...
         'Same consequence as above for K_opt.'], lambdaPeak, P.lambda_opt);
end

if abs(betaPeak) > 1e-6
    warning('wtGenerateAeroTables:BetaPeak', ...
        'Peak Cp occurs at beta = %.2f deg, expected 0 deg (fine pitch).', ...
        betaPeak*180/pi);
end

end
