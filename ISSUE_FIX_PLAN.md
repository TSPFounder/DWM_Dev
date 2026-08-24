# DWM_Dev — Issue Fix Plan

Covers all 16 open issues (#5–#20) as of 2026-08-24, against `origin/master` @ `af2d366`.

Each issue already carries a per-issue analysis comment. This document does not repeat those.
It does three things they cannot do individually: it **verifies their claims against the code
that is actually in the repository**, it **groups and sequences** the work, and it **separates
what can be started now from what is blocked**.

---

## 0. Read this first — six analyses cite code that is not in this repository

Before planning around the issue comments, every symbol they name was checked against
`origin/master`. Most resolve. Four do not:

| Symbol cited in an issue comment | In `Source/DWM_Dev` on master? | Cited by |
|---|---|---|
| `ADwmNpcActor::ConfigureProfileFromSource` | **No** | #9, #10, #17, #19 |
| `ApplyBasicIdleAnimationToSourceActor` (in `DWM_DevGameMode.cpp`) | **No** | #19 |
| `DwmValleyLifeDirector.cpp` | **No** | #18 |
| `MariaStandToSitAnimation` / `MariaSitIdleAnimation` | **No** | #13 |

There is also no `[DWM Hillside]`-tagged bootstrap loop in `DWM_DevGameMode.cpp`; the string
"Hillside" appears in `Source/` only in comments and in one map name constant. The repository has
two branches, `master` and the already-merged `codex/publish-recent-dwm-updates`, so this is not a
case of the work sitting on a feature branch.

Reproduce the check:

```bash
git fetch origin master && git checkout origin/master
grep -rn "ConfigureProfileFromSource\|ValleyLifeDirector\|MariaStandToSit\|ApplyBasicIdleAnimation" Source/
```

**What this means.** The Hillside / Valley / City NPC bootstrap layer exists on someone's machine
but has never been pushed. The analyses for #9, #10, #13, #17, #18 and #19 were written against
it, so their diagnoses may well be correct — but nothing in this repository can confirm them, and
nobody can implement those fixes from a fresh clone.

This is the same failure shape recorded repeatedly across this project: **a confident report
produced without checking the thing it describes.** The comments read as verified findings; four
of them are unverifiable from the repository.

> ### P0 — prerequisite for six issues
> Push the NPC bootstrap layer (`ConfigureProfileFromSource`, the Hillside configuration pass,
> `DwmValleyLifeDirector`, the Maria animation fields) to `master`. Until that lands, **#9, #10,
> #13, #17, #18 and #19 cannot be worked from this repository at all** — not "are harder", cannot.
>
> When it lands, re-read those six analyses against the real code before implementing. Diagnoses
> written against an unpushed tree are hypotheses.

Issues whose cited code **is** present and was confirmed line-by-line: **#5, #6, #7, #8, #11, #12,
#14, #15, #16, #20**. Those are actionable today.

---

## 1. Blocked on your input — decide before the sprint starts

Two items cannot be specified by reading code. Answer these first; they are cheap to answer and
expensive to guess at.

**#15 — removed "how to get to the next level" dialogue.**
The analysis searched and could not find a removed-in-code candidate. `PopulateDefaultHankDialogue`
is intact on master, so whatever was removed was either never in C++ (a Blueprint/DataTable line)
or was removed before the current history. Needed from you: **which NPC said it, in which level,
and roughly what it said.** Without that, any "restoration" is new writing wearing the word
"restore".

**#12 — transition messaging (second half of the issue).**
The suspension half is diagnosed and actionable (§3). The messaging half is not: nothing states
what the current text says versus what it should say. Needed from you: **the current wording and
the intended wording.**

---

## 2. Workstreams

The sixteen issues are not sixteen independent tasks. They are five clusters plus two singletons,
and the clustering is what makes the sequencing worth having.

| Workstream | Issues | Kind | Blocked by |
|---|---|---|---|
| **A. Interaction & terminals** | #20, #6 | C++ + placement | — |
| **B. Level transitions & instancing** | #12, #14 | C++ | #12 messaging → your input |
| **C. Level dressing** | #5, #16, #7, #11 | Editor only, no code | — |
| **D. NPC animation** | #8, #9, #13, #18, #19 | C++ | #8 free; rest → **P0** |
| **E. Character identity** | #17 | Content | **P0** |
| **F. Dialogue content** | #15 | Content | your input |
| **G. NPC facing** | #10 | Investigation | **P0** |

Workstreams **A, B (partially) and C** are unblocked. **D is unblocked only for #8.** Everything
else waits on P0 or on you.

---

## 3. Per-issue plan

Each entry states **how well the cause is actually known**, which is the part most likely to be
wrong later.

### Workstream A — Interaction & terminals

#### #20 — Terminals need a key other than E — **cause CONFIRMED in code**

Two separate bindings claim `E`, and they chain:

- `DWM_DevCharacter.cpp:73` — `BindKey(EKeys::E, IE_Pressed, …, &ADWM_DevCharacter::HandlePrimaryInteraction)`
- `DWM_DevPlayerController.cpp:29` — `BindKey(EKeys::E, IE_Pressed, …, &ADWM_DevPlayerController::InteractWithTradeTerminal)`,
  which at `:82` casts the pawn and calls `HandlePrimaryInteraction()` anyway.

`HandlePrimaryInteraction` (`DWM_DevCharacter.cpp:172–237`) resolves in this order: advance open
dialogue → 350 cm `TActorIterator<ADwmNpcActor>` sweep → begin dialogue → trade terminal → "Nothing
to interact with." **The terminal branch is last**, so within 350 cm of any NPC, E can never reach
a terminal.

Worse, and not noted in the issue comment: the fallback sweep at `:188–210` assigns `ActiveNpc`
**directly**, bypassing `SetActiveNpc`/the NPC's overlap registration. `ClearActiveNpc`
(`:164`) only fires from the NPC's end-overlap — the very handoff the fallback exists because it
was missed. So `ActiveNpc` plausibly **latches for the rest of the session**, making the terminal
branch unreachable everywhere, not just near an NPC. Confirm this with a log in `ClearActiveNpc`
before building on it.

The sweep also has no line-of-sight test, so it selects NPCs through walls.

**Fix**
1. Give terminals their own key — `T` (Trade). Bind it in **one** place to a new
   `ADWM_DevCharacter::InteractWithTradeTerminal()` that only ever touches
   `ActiveTradeTerminal`.
2. Remove the terminal branch from `HandlePrimaryInteraction`, leaving E as dialogue-only.
3. Delete the duplicate `E` binding at `DWM_DevPlayerController.cpp:29` and its
   `InteractWithTradeTerminal` trampoline, which no longer does what its name says.
4. Fix the `ActiveNpc` latch: route the fallback sweep through `SetActiveNpc`, and clear it when
   the nearest NPC leaves the radius.
5. Add a line-of-sight trace to the sweep.

**Do not regress the proven path.** The E-key trade-terminal interaction was verified five times
on Day 20. Steps 1–3 move that behaviour to a new key rather than reworking it; keep the
`ExecuteTrade(this)` call site byte-identical.

**Also check the content side.** `B_Door` (from `AmericanCityPacks`) carries its own `InputKey E`
node with Consume Input on, which independently steals E in the City level. Retarget it to `F`.
Note that `Find in Blueprints` searching for "InputKey E" matches comment text and will report
false positives — inspect the graph, do not trust the search.

**Verify:** in each of the four community levels, stand beside an NPC and a terminal
simultaneously; E must open dialogue every time, T must trade every time, in either order, and
after walking away and back.

#### #6 — Trade terminals appear in all four levels — **cause LIKELY (placement)**

`ADwmTradeTerminalActor` has no programmatic spawn path; instances are level-placed. Per-instance
`ToCommunityId`/`FromCommunityId` config already exists (Day 20), so **no code change is needed.**

**Fix:** per level, keep exactly one terminal at the narratively correct trade location, delete
test leftovers, and place the relevant prop beside it.

**Sequencing:** do #6 **after** #20. Moving terminals out of NPC radii masks the E conflict without
fixing it, and a masked bug is worse than a visible one.

### Workstream B — Level transitions & instancing

#### #12 — Player suspended too high on arrival — **cause CONFIRMED in code**

`UDwmGameInstance::ApplyRouteSpecificTransitionArrival` (`DwmGameInstance.cpp:633`) teleports to
the marker's raw transform:

```cpp
PlayerPawn->SetActorLocationAndRotation(
    ArrivalMarker->GetActorLocation(), ArrivalRotation, false, nullptr, ETeleportType::TeleportPhysics);
```

There is no ground trace. `ADwmNpcActor::ProjectToGround` already solves exactly this for NPCs, so
the player is the only thing in the game that arrives at an author-placed Z with no correction. A
marker placed a metre high puts the player a metre high.

**Fix:** downward `LineTraceSingleByChannel` from the marker (start ~200 cm above, end ~500 cm
below), place the pawn at `Hit.Location + capsule half-height`, and fall back to the raw marker
location with a `UE_LOG` warning when the trace misses. Mirror `ProjectToGround`'s conventions
rather than inventing new ones — and log the miss, don't silently fall through.

**Verify:** every route pair in both directions; the player must land standing, not falling.

*(Messaging half: blocked — see §1.)*

#### #14 — Second Mountain instance's rotors render in the first — **cause HYPOTHESIS ONLY**

Flagged as a hypothesis in the issue comment, and it stays one here. Two concrete defects in
`UDwmGameInstance::SpawnWorldActors` (`DwmGameInstance.cpp:2436`) are consistent with the symptom:

1. **Fixed world-space spawn, no level-instance awareness:**
   `const FVector SpawnLocation(SpawnCount * 300.0f, 0.0f, 200.0f);` — absolute coordinates,
   ignoring the instance's transform. Every instance spawns its actors on top of the first.
2. **Fixed actor names:** `SpawnParams.Name = FName(*FString::Printf(TEXT("DwmActor_%s"), *Block.BlockId));`
   A second instance requesting the same name while the first actor lives collides.

**Also spotted here, unrelated to the issue but worth fixing while open:** the success log at
`:2495` is `TEXT("[DWM] Spawned '%s' at (1160 0, 70).")` with **two** arguments passed
(`*Block.Name, SpawnLocation.X`) against **one** `%s`, and the coordinates are hardcoded into the
literal. It prints a fixed, wrong position for every spawn — a log that will actively mislead the
next person debugging this.

**Fix**
1. **Confirm the hypothesis before writing the fix.** Log `GetWorld()`, the owning level, and the
   final spawn transform per actor, run both Mountain instances, and check whether the actors are
   genuinely co-located or merely look it.
2. Then: transform `SpawnLocation` into the owning level instance's space, and make the actor name
   unique per instance (or drop `SpawnParams.Name` and let UE assign one).
3. Fix the log statement.

### Workstream C — Level dressing (no code)

These four need no C++ and can run fully in parallel with everything else. Confirmed: no
`SpawnBox` class, no fencing system, and no shop-interior logic exists in `Source/DWM_Dev`.

#### #5 — Spawn boxes visible in PIE
Red wireframe in PIE = a collision shape still rendering. Select the box in the outliner, find the
Box/Capsule component, and set **Hidden in Game** (or clear **Visible**). Collision is untouched.
**Fix it on the Blueprint default, not per instance,** or it will regress in the next level.

#### #16 — Suburbs spawn box re-entered while walking to DeShawn
Same actor category as #5, placement rather than visibility. Move or resize the volume so the
route to the meeting place cannot re-enter it. **Do #5 first** — an invisible volume is much
easier to reason about once you can toggle its debug draw deliberately.

#### #7 — Complete the Mountain fencing
Identify gaps in the existing run, place matching segments from the same pack at the same spacing.
Check for corner and gate pieces — usually separate meshes, and the usual thing to miss.

#### #11 — Hillside Pizza shop is empty
Furnish with tables/chairs, a counter with a register, and kitchen equipment. Prefer packs already
used elsewhere in Hillside so it reads as the same visual family. Leave clear floor in front of the
counter for the terminal or NPC marker from #6.

### Workstream D — NPC animation

#### #8 — Hank rotates without stepping — **cause CONFIRMED in code**  *(the one D item that is unblocked)*

`StepTowardTarget` already handles this correctly for the *start* of a walk
(`DwmNpcActor.cpp:414–415`): it computes `SignedFacingError`, calls
`SetTurningInPlace(bNeedsTurn, …)`, then `FaceDirection`. Arrival does not get the same treatment.
`TickMovement`'s `IdleAtMarker` case (`:330–335`) instead does:

```cpp
const FRotator Current = GetActorRotation();
SetActorRotation(FMath::RInterpConstantTo(Current, HomeRotation, DeltaSeconds, TurnSpeed));
```

Raw `SetActorRotation` every tick, no `SetTurningInPlace`. And `RefreshLocomotionAnimation`'s
`bMoving` test (`:598–601`) covers only `WalkingToTurbine`, `Wandering` and `ReturningHome` — not
`IdleAtMarker`. So on arrival the idle clip is already selected while the body keeps rotating
underneath it. That is exactly "rotates without steps".

**Fix**
1. In the `IdleAtMarker` case, use the `StepTowardTarget` pattern: signed facing error to
   `HomeRotation` → `SetTurningInPlace(bNeedsTurn, SignedFacingError < 0.0f)` → `FaceDirection`.
   Do not call `SetActorRotation` directly.
2. Once within `MovementFacingToleranceDegrees`, `SetTurningInPlace(false)` to settle into idle.
3. No new animation assets — `TurnLeftAnimation`/`TurnRightAnimation` are already wired.

**Verify:** watch Hank complete a full round trip. Feet must move whenever the body does, at both
ends of the walk.

#### #9 — Hillside NPCs move in lockstep — **BLOCKED on P0**
Analysis attributes it to `ConfigureProfileFromSource` loading one shared idle clip and
`PlayAnimation(Clip, true)` starting every instance at frame 0 with no offset. Plausible and
matches the symptom — but that function is not in the repository.
**After P0:** add a per-instance `SetPosition(FMath::FRandRange(0, Clip->GetPlayLength()))` after
`PlayAnimation`; if desyncing one clip isn't enough, pick per NPC from a pool of 2–3 idles.

#### #13 — Valley NPC rocks oddly in the rocking chair — **BLOCKED on P0**
Analysis: `MariaStandToSitAnimation`/`MariaSitIdleAnimation` are `Office_Desk_*` clips reused on
`SM_RockingChair`. Neither field exists on master.
**After P0:** either source a rocking-chair clip, or align the chair's seat/pivot to the office
clip's root motion. The same office-clip-on-wrong-furniture pattern caused the Kai desk-pose
problems, so fix the class of bug, not the instance.

#### #18 — Mike needs a different animation — **BLOCKED on P0**
Analysis proposes `DwmValleyLifeDirector.cpp` as the template. That file is not in the repository.
**After P0:** prefer generalising the existing director over cloning a near-identical second class.
Small feature; lowest priority in this workstream.

#### #19 — Kai's hand animation is wrong — **BLOCKED on P0 (partially reproducible now)**
Analysis blames `ApplyBasicIdleAnimationToSourceActor` forcing
`SetAnimationMode(EAnimationMode::AnimationSingleNode)` while a Post Process AnimBP fights it —
"weird position, then vibrates fast". That function is not in the repository, but the same
single-node forcing **is** present at `DwmNpcActor.cpp:621` and `:652`, and `:243` sets
`AnimationBlueprint` mode — so the conflict is real in the code that is here.
**Cheap test available now, no P0 needed:** tick **Disable Post Process Blueprint** on Kai's mesh.
If the contortion stops, the diagnosis is confirmed and the fix is to stop forcing single-node mode
on a mesh that has a post-process AnimBP.

### Workstream E — Character identity

#### #17 — DeShawn is identical to Kai — **BLOCKED on P0; content, not code**
The `DWM_Kai` / `DWM_DeShawn` tokens resolve correctly; the placed source actor carries the same
mesh. **Fix:** give DeShawn a distinct CitySampleCrowd mesh/material on his placed source actor.
Set `RandomOptions = false` so the choice sticks. Blocked only because the analysis leans on
`ConfigureProfileFromSource` for how the mesh reaches the NPC.

### Workstream G — NPC facing

#### #10 — Hillside NPCs don't face the player — **CAUSE NOT FOUND; BLOCKED on P0**

The issue comment is candid that it could not pin this down, and that honesty is the most useful
thing in it. The generic path is confirmed present: `ADwmNpcActor::Tick` checks
`Activity == EDwmNpcActivity::Talking` (set unconditionally in `BeginDialogue`) and calls
`FaceDirection(ToListener, DeltaSeconds)` toward `CurrentListener` every frame, independent of
`bEnableScriptedMovement`. Hillside NPCs use the same class, so there is **no code-level reason
this should fail** — which means the cause is in the part that is not in the repository, or in
content.

**Do not write a fix. Instrument first**, in this order:
1. Check `TurnSpeed` on the Hillside instances — a near-zero value turns correctly but
   imperceptibly.
2. `UE_LOG` in the `Talking` branch of `Tick()`, printing `CurrentListener` and the computed facing
   error. Confirm it actually executes for a Hillside NPC in a real conversation. This
   distinguishes "not turning" from "never reaching `Talking`", which are different bugs.
3. If both check out, compare the mesh's forward axis against `HomeRotation` in the editor —
   a mismatched root-bone convention would make a correct turn look like no turn.

Report what those show before any code changes.

---

## 4. Sequencing

**Now, in parallel:**
- **P0** — push the NPC bootstrap layer. Unblocks six issues; nothing else in workstreams D/E/G
  moves until it lands.
- Answer §1 (#15 wording, #12 messaging).
- **#20** then **#6** — in that order.
- **#8** — self-contained, confirmed, no dependencies.
- **#5** then **#16**; **#7**; **#11** — editor work, fully parallel.
- **#19's** post-process test — no P0 needed, one checkbox.

**After P0 lands:** re-read the six analyses against the real code, *then* #9, #13, #17, #19, #18,
and #10's instrumentation.

**After §1 answers:** #12's messaging, #15.

**#14 last** — its cause is a hypothesis, so it needs an instrumented run before any fix, and the
level-instance question may interact with #12's arrival logic.

---

## 5. Verification

Two rules, both earned the hard way on this project.

**A cause is not confirmed until it has been observed.** Three of these issues (#10, #14, and #19's
mechanism) currently have *hypotheses*, and this document labels them as such. Do not let a
hypothesis become a premise by being restated. Where a fix depends on an unconfirmed cause,
instrument first and let the observation decide.

**Check the thing itself, not a proxy for it.** The four missing symbols in §0 were reported as
findings without anyone checking whether the code existed. The same shape has appeared here as
tools reporting success they never verified, and as `Find in Blueprints` matching comment text and
being believed. Before reporting any of these issues fixed, run it in PIE and watch it — a
successful compile is not a working fix.

**Per-issue exit criteria** are stated in each entry above. Every issue in workstream C is verified
by looking at the level; every issue in A, B and D is verified by playing it.
