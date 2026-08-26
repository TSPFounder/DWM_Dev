function [Cp, Ct] = wtCp(lambda, beta, P)
%WTCP  Power and thrust coefficients as a function of tip-speed ratio and pitch.
%
%   [CP, CT] = WTCP(LAMBDA, BETA, P) evaluates the rotor power coefficient
%   Cp(lambda,beta) and thrust coefficient Ct(lambda,beta).
%
%   LAMBDA  tip-speed ratio, omega_r * R / v_rel          [-]
%   BETA    collective blade pitch angle                  [rad]
%   P       parameter struct from WTPARAMETERS
%
%   Both inputs may be arrays (element-wise, they must broadcast).
%
%   THE EQUATION
%   ------------
%   Standard empirical fit (Heier):
%
%       1/lambda_i = 1/(lambda + 0.08*beta_deg) - 0.035/(beta_deg^3 + 1)
%
%       Cp = c1*(c2/lambda_i - c3*beta_deg - c4)*exp(-c5/lambda_i)
%            + c6*lambda
%
%   with beta in DEGREES inside the fit (the coefficients are defined that
%   way), which is why the conversion appears on the first line below. This
%   is a frequent source of a silently wrong Cp curve.
%
%   Thrust uses the momentum-theory relation between the axial induction
%   factor a and Cp, closed with the Glauert empirical correction above
%   a = 0.4 where simple momentum theory stops being valid.
%
%   VALIDITY
%   --------
%   This is a GENERIC utility-scale fit, not measured data for the rotor in
%   the BOM. It reproduces the right Cp_max, lambda_opt and general shape,
%   which is what the control loops need, but it is not a substitute for a
%   BEM-derived Cp surface from the actual blade geometry (airfoil series,
%   chord and twist distribution). See the limitations section of
%   WTRUNSIMULATION.
%
%   See also WTPARAMETERS, WTGENERATEAEROTABLES.

if nargin < 3
    P = wtParameters();
end

c = P.cp;

% The fit's coefficients are defined for beta in degrees.
betaDeg = beta * 180/pi;

% Guard lambda away from zero: at standstill the fit is singular, and the
% model must still evaluate during start-up and shutdown transients.
lambdaSafe = max(lambda, 1e-3);

invLambda_i = 1 ./ (lambdaSafe + 0.08*betaDeg) - 0.035 ./ (betaDeg.^3 + 1);

% invLambda_i can go negative or through zero at high pitch / low lambda,
% which would produce a nonsense exponential. Clamp to a small positive value.
invLambda_i = max(invLambda_i, 1e-6);

Cp = c.c1 .* (c.c2.*invLambda_i - c.c3.*betaDeg - c.c4) .* ...
     exp(-c.c5.*invLambda_i) + c.c6.*lambdaSafe;

% A negative Cp is physically meaningful only as a braking (windmilling)
% state; for this model clamp at zero so the turbine never motors the wind.
Cp = max(Cp, 0);

if nargout > 1
    % Axial induction from Cp via momentum theory: Cp = 4a(1-a)^2.
    % Solve the cubic approximately by inverting the low-a branch, then
    % apply the Glauert correction for the turbulent-wake state.
    a = 0.5 * (1 - sqrt(max(1 - Cp./0.593, 0)));   % Betz-normalised
    a = min(max(a, 0), 0.6);

    Ct = 4.*a.*(1 - a);                              % momentum theory
    glauert = a > 0.4;
    Ct(glauert) = 4.*a(glauert).*(1 - 0.25*(5 - 3*a(glauert)).*a(glauert));

    Ct = min(max(Ct, 0), 1.2);
end

end
