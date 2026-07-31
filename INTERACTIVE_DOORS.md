# Interactive Doors

This project uses the reusable C++ class `ADwmInteractiveDoor` for hinged, player-operated
doors. Its Blueprint children use **F** to open and close; **E** is reserved for the trade
terminal.

Relevant source files:

- `Source/DWM_Dev/DwmInteractiveDoor.h`
- `Source/DWM_Dev/DwmInteractiveDoor.cpp`
- `Source/DWM_Dev/DWM_DevCharacter.cpp`

## Before starting

1. Use a **door-only** mesh. The frame, wall, and doorway should remain static.
2. Find every actor which visually belongs to the door: the panel, separate glass, decals,
   signs, handles, and hardware.
3. Record the original panel's Location, Rotation, Static Mesh, and material overrides.
4. If the original mesh includes both the door and its frame, do not use it as a hinged door:
   the entire assembly would rotate.

## Create a door Blueprint

1. In the Content Browser, create a Blueprint Class using `DwmInteractiveDoor` as the parent,
   or duplicate an existing interactive-door Blueprint.
2. Give each visual style its own asset, for example `BP_PizzaNapoliDoor`.
3. Open the Blueprint and select `Door Mesh`.
4. In **Details > Static Mesh**, assign the source door panel mesh.
5. Copy any material overrides from the original door actor to the matching material slots.
6. Click **Compile**, then **Save**.

`BP_InteractiveDoor` may initially open as a data-only Blueprint. Click **Open Full Blueprint
Editor** at the top of that window before adding child components.

## Place and configure the door

1. Keep the original door in the level while positioning the replacement.
2. Place the interactive-door Blueprint at the original door's location.
3. Copy all three Location values and all three Rotation values from the original door into the
   Blueprint instance. This preserves the original handle side and hinge side.
4. In the Blueprint instance's **DWM > Door** section, set:
   - **Open Yaw Degrees** to `90` or `-90`.
   - **Open Speed Degrees Per Second** to the desired speed.
   - **Interaction Radius** to the desired player range.
5. Test with **F**. If the door opens outward rather than inward, change only the sign of
   **Open Yaw Degrees** (`90` to `-90`, or the reverse). Do not rotate the placed actor solely
   to change its swing direction.

## Add separate glass, decals, and hardware

The preferred method is to make each visual item a component of the Blueprint, attached to the
`Hinge` component. That way it follows the door automatically.

1. Open the full Blueprint Editor.
2. In the **Components** panel, select `Hinge`.
3. Choose **+ Add > Static Mesh**.
4. Name the component clearly, such as `DoorGlass`, `PizzaDecal`, or `DoorHandle`.
5. Confirm the component is indented beneath `Hinge` in the Components tree.
6. Select the new component and assign its Static Mesh and materials.
7. Use the Blueprint **Viewport** to place it relative to the door:
   - red arrow: local X;
   - green arrow: local Y / depth;
   - blue arrow: local Z / height.
8. For a flat decal mesh, move it only slightly out from the glass so it is not hidden inside the
   door. Rotate its local blue (Yaw) value by `180` degrees if its visible side faces inward.
9. Set every added visual component's collision to **No Collision**.
10. **Compile** and **Save** after changes.

Use the source actor's **Browse to Asset** button to reveal the correct mesh in the Content
Browser. For example, `SM_StoreDoor01_Glass4` is a placed actor instance; the corresponding
mesh asset is `SM_StoreDoor01_Glass`.

## Retire the original level pieces

Do not remove the originals until the replacement matches visually and works in PIE.

For every old door panel, old glass panel, old decal, or old hardware actor:

1. Enable **Actor Hidden In Game**.
2. Set **Collision Enabled** to **No Collision**.
3. Save the level.

Hiding an actor does **not** necessarily remove its collision. An old hidden door or glass pane
can leave an invisible wall across an otherwise open doorway. Delete old actors only after the
replacement has passed testing.

## Collision troubleshooting

1. Run PIE and open the door with **F**.
2. Press `~`, type `show collision`, and press Enter.
3. The door panel's collision should rotate with the open door.
4. A second collision shape which remains across the opening belongs to an old/duplicate actor.
   Stop PIE and set that actor to **No Collision**.
5. Keep collision enabled on the new `DoorMesh`; set **No Collision** only on decorative child
   components and retired original actors.

If the Collision Presets list does not provide the required option, select **Custom** first,
then set **Collision Enabled** to **No Collision**.

## Final verification checklist

- [ ] Door appears in the correct opening, with the correct material, handle side, and hinge side.
- [ ] `F` opens and closes it from the expected interaction range.
- [ ] It opens inward and does not spawn the player inside geometry.
- [ ] The player can pass through while open.
- [ ] Glass, logo, handle, and other child components swing with the door.
- [ ] Decorative child components have **No Collision**.
- [ ] Old hidden door/glass/decal actors have **No Collision**, or have been deleted.
- [ ] The Blueprint and affected level are both saved.

## Duplicating a finished door

To create a variation without changing the existing one:

1. Select the finished Blueprint in the Content Browser.
2. Right-click and choose **Duplicate** (or press `Ctrl+W`).
3. Rename the copy.
4. Change only its meshes, materials, decals, and default visual components.
5. Compile and save the copy.

All Blueprint copies retain the same C++ hinge behavior and F-key interaction.

## Convert selected static meshes automatically

After building the editor module and restarting Unreal Editor, select one or more door-panel
`StaticMeshActor` instances in the World Outliner, then choose:

**Tools > Dream World Maker > Convert Selected Static Meshes to Interactive Doors**

The command creates a new `ADwmInteractiveDoor` for each selected actor, preserving its mesh,
material overrides, transform, and collision profile. It retains the source actor as a reversible
reference, but hides it in game and changes its collision to **No Collision**.

The command deliberately does not guess whether a mesh is a door, which edge is its hinge, or
which nearby glass/decal actors belong to it. After each conversion, verify the mesh is door-only,
set **Open Yaw Degrees** to `90` or `-90`, and add any separate visuals as `Hinge` children.
