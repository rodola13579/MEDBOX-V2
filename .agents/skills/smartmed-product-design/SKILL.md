---
name: smartmed-product-design
description: Designs and maintains the SmartMed smart medicine dispenser, including its physical 3D structure, eight-compartment rotation mechanism, medication access position, healthcare dashboard integration, and product visualization. Use whenever modifying the medicine box model, compartment system, rotation animation, medication states, dashboard visualization, or SmartMed product behavior.
---

# SmartMed Product Design Skill

## 1. Purpose
This skill defines the physical product, mechanical behavior, visual language, and interaction rules of the SmartMed smart medicine dispenser.

The AI must preserve the real-world mechanism when modifying the 3D model or dashboard.

Do not simplify the product into a flat pie chart.

## 2. Physical Product Specification
SmartMed is a circular cylindrical smart medicine dispenser containing exactly:
- 8 radial compartments
- Each compartment occupies 45 degrees
- 7 compartments normally contain medicine
- 1 compartment is the fixed access/open position
- The entire circular tray rotates around a central axis

The visualization must look like a real physical healthcare device rather than a chart.

## 3. Critical Mechanical Rule
The eight compartments belong to ONE rotating tray. Individual compartments DO NOT rotate independently.

Correct behavior:
1. Determine the target compartment.
2. Rotate the entire tray.
3. Move in increments of 45 degrees.
4. Stop when the target compartment reaches the fixed front access position.
5. Enter READY state.
6. User takes medicine.
7. Record the event.

The camera remains fixed while the tray rotates.

## 4. Fixed Access Position
The front access position is fixed relative to the user.

When viewed from above:
- Front/user direction ≈ 6 o'clock
- The empty/access position faces the user
- The access position does not rotate with the tray

In 3D perspective, the empty access area must visually face the camera/user.

## 5. Rotation Logic
There are exactly eight positions:
0, 1, 2, 3, 4, 5, 6, 7

with:
`positionAngle = positionIndex * 45`

Normalize rotation to a valid 45-degree final position. Avoid floating-point drift.

## 6. Product States
Use:
- IDLE
- ROTATING
- READY
- TAKEN
- ERROR

ROTATING example:
"Đang xoay đến Ngăn 5..."

READY example:
"Sẵn sàng lấy thuốc"

## 7. 3D Camera
Use a product-rendering perspective:
- 3/4 front view
- slightly above
- approximately 25–35 degrees downward
- slight horizontal perspective
- visible top and side
- front access position clearly visible

Avoid perfectly top-down or flat views.

## 8. 3D Geometry
Use:
- cylindrical body
- rounded/beveled edges
- visible thickness
- central shaft/axis
- tray surface
- compartment walls
- lid/upper structure where appropriate
- subtle LED ring

Avoid flat SVG-like circles, flat pie charts, floating compartments, or sharp primitive-only geometry.

## 9. Compartment Visualization
Each compartment should communicate:
- compartment number
- medicine identity
- quantity
- current state

Possible states:
- MEDICINE_AVAILABLE
- ACCESSIBLE
- EMPTY
- UPCOMING
- TAKEN
- ERROR

Use subtle pastel healthcare colors.

## 10. LED Ring
Use a subtle blue LED ring:
- IDLE: soft static glow
- ROTATING: subtle animated glow
- READY: stronger indication
- ERROR: warning indication

Avoid excessive glow.

## 11. Dashboard Integration
Prioritize:
1. Current medicine
2. Next scheduled medication
3. Current compartment
4. Device state
5. Today's medication schedule
6. Inventory
7. Medication history

The 3D visualization supports the dashboard rather than replacing it.

## 12. Data Model
Separate scheduling/business logic from the 3D renderer.

Example:
```javascript
const medicineBoxState = {
  currentCompartment: 3,
  targetCompartment: 5,
  rotationState: "ROTATING",
  compartments: [
    { id: 0, medicine: "Vitamin C", quantity: 12, status: "AVAILABLE" }
  ],
  nextMedication: {
    name: "Vitamin C",
    dose: "1 viên",
    time: "20:00",
    compartment: 5
  },
  deviceStatus: {
    wifi: true,
    battery: 86,
    motor: "OK",
    lid: "CLOSED"
  }
};
```

Do not put scheduling logic directly inside the 3D geometry component.

## 13. Interaction Rules
When a scheduled medication is selected:
1. Identify target compartment.
2. Display target medication.
3. Start tray rotation.
4. Keep camera fixed.
5. Rotate entire tray.
6. Stop exactly at target.
7. Highlight accessible compartment.
8. Change state to READY.
9. Allow medicine-taking interaction.

## 14. Healthcare Product Aesthetic
The product should feel:
- premium
- clean
- trustworthy
- calm
- modern
- minimal
- medical/healthcare
- consumer IoT

Prefer white, light blue-gray, soft blue, green, pastel compartment colors, and dark gray text.

Avoid cyberpunk, gaming UI, excessive neon, excessive gradients, excessive glassmorphism, and overly dark interfaces.

## 15. Engineering Constraints
Before modifying the visualization:
1. Inspect the existing project.
2. Identify framework and 3D library.
3. Identify state source and animation implementation.
4. Preserve existing architecture whenever possible.
5. Prefer reusable components and centralized constants.

## 16. Quality Checklist
- Exactly 8 compartments
- Each compartment is 45 degrees
- Entire tray rotates together
- Individual compartments do not rotate independently
- Front access position is fixed
- Target reaches front access position
- Camera remains fixed during rotation
- Rotation stops at a valid position
- Product has real 3D depth
- Product does not look like a pie chart
- READY state is obvious
- 3D renderer is separated from scheduling logic

## 17. Priority Rule
When visual decoration conflicts with mechanical correctness, mechanical correctness wins.

When visual complexity conflicts with user understanding, user understanding wins.

The SmartMed product specification wins over generic UI patterns.
