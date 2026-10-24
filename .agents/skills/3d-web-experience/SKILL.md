---
name: 3d-web-experience
description: Builds high-quality interactive 3D web experiences for the SmartMed medicine dispenser using Three.js, React Three Fiber, WebGL, animation systems, lighting, materials, camera control, and state-driven product visualization.
---

# 3D Web Experience Skill

## Objective
Create a high-quality real-time 3D product experience for SmartMed. The model must resemble a real manufactured healthcare device, not a flat infographic.

## Technology
When supported by the existing project, prefer:
- Three.js
- React Three Fiber
- @react-three/drei
- existing animation libraries

Before installing anything:
1. Inspect package.json.
2. Check existing dependencies.
3. Reuse installed libraries.
4. Avoid unnecessary packages.

## SmartMed Geometry
The dispenser contains:
- 8 radial compartments
- 8 × 45 degrees
- 1 circular rotating tray
- 1 fixed front access position
- 1 central axis

The compartments belong to one physical rotating tray.

## Scene Structure
Prefer:
Scene
├── Camera
├── Lights
├── MedicineBox
│   ├── Base
│   ├── RotatingTray
│   │   ├── Compartment0
│   │   ├── ...
│   │   └── Compartment7
│   ├── CentralAxis
│   ├── AccessOpening
│   └── LEDRing
└── Environment

## Coordinate System
Prefer:
Y = vertical
X/Z = horizontal plane

Rotate the tray around the vertical Y axis.

## Compartment Placement
For index:
```javascript
angle = index * Math.PI / 4;
x = Math.cos(angle) * radius;
z = Math.sin(angle) * radius;
```
Adapt signs to the project's coordinate convention while preserving equal 45-degree spacing.

## Fixed Access Position
The access opening belongs to the static product structure. It does not rotate with the tray.

The target compartment rotates into this location.

## Camera
Use a perspective camera:
- 3/4 front
- slightly above
- approximately 25–35 degrees downward
- moderate perspective
- fixed during medication rotation

Example starting position only:
```javascript
position ≈ (6, 5, 7)
```

## Product Geometry
Use:
- cylinders
- extruded radial walls
- rounded boxes
- beveled edges
- central shaft
- top surface
- side wall
- bottom base

Avoid sharp primitive-only geometry.

## Materials
Prefer physically based materials such as MeshStandardMaterial when appropriate.

Use moderate roughness, subtle metallic values, and clean plastic/medical-device appearance. Do not make everything metallic.

## Lighting
Use soft product lighting:
- key light
- fill light
- subtle rim light
- ambient/environment lighting

Goal:
clear geometry + soft shadows + premium appearance

## Shadows
Use shadows where performance permits. Shadows should communicate physical depth.

## LED Ring
A subtle LED ring may use torus geometry and emissive material.

States:
- IDLE: soft static
- ROTATING: subtle animation
- READY: stronger indication
- ERROR: warning indication

Avoid excessive bloom.

## Rotation Animation
Never animate individual compartments separately.

Correct:
```javascript
rotatingTray.rotation.y = targetRotation;
```

Incorrect:
```javascript
compartment5.rotation.y = ...
```

## Discrete Rotation
Use:
```javascript
const STEP = Math.PI / 4;
const targetRotation = targetIndex * STEP;
```

Final position must be exact. Avoid accumulated floating-point rotation drift.

## Animation States
Use:
- IDLE
- ROTATING
- READY
- TAKEN
- ERROR

The 3D component visualizes state. It must not invent medication state.

## Highlight Target
During rotation:
- subtle highlight
- target indicator
- optional label

After rotation:
- target compartment is clearly accessible
- LED changes
- UI becomes READY

Avoid flashing effects.

## Camera Interaction
Optional user orbit/zoom is acceptable, but automatic medication positioning must work independently.

During automatic rotation:
- camera stays stable
- tray rotates
- target reaches access position

Do not automatically orbit the camera.

## Performance
Prefer:
- instancing where useful
- moderate polygon counts
- compressed textures
- lazy loading
- reusable geometry/materials
- limited lights
- controlled shadows

Avoid unnecessary high-poly geometry and expensive post-processing.

## Responsive Canvas
On mobile:
- reduce rendering resolution if appropriate
- reduce shadows if necessary
- preserve silhouette
- preserve access position visibility

## React Architecture
If React Three Fiber is used, prefer:
```text
components/
└── MedicineBox3D/
    ├── MedicineBox3D.jsx
    ├── RotatingTray.jsx
    ├── Compartment.jsx
    ├── AccessOpening.jsx
    ├── LEDRing.jsx
    └── useMedicineBoxAnimation.js
```

Do not put the whole 3D application in one component.

## State Separation
The 3D scene should receive:
```javascript
{
  currentCompartment,
  targetCompartment,
  rotationState,
  deviceStatus
}
```

Do not put scheduling, database, authentication, notification, or inventory business logic inside the 3D renderer.

## Visual Quality Checklist
- Device has real thickness
- Device has visible side walls
- Compartments have physical depth
- Exactly 8 compartments
- Tray rotates as one object
- Access opening is static
- Camera is stable
- Target reaches access position
- Rotation stops exactly
- Materials are realistic
- Shadows communicate depth
- LED is subtle
- Scene performs well
- Mobile remains usable
- No flat pie-chart appearance
