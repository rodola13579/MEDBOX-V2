---
name: web-design-guidelines
description: Audits and improves SmartMed web interfaces for visual quality, UX consistency, accessibility, responsive behavior, interaction quality, healthcare usability, and production readiness.
---

# Web Design Guidelines Skill

## Purpose
Act as a UI/UX quality reviewer.

Do not blindly rewrite the application. Inspect first, identify problems, then make focused improvements.

## Review Order
1. Functional correctness
2. Information hierarchy
3. UX
4. Accessibility
5. Responsive layout
6. Visual consistency
7. Animation
8. Performance
9. Micro-polish

Do not prioritize decoration over functionality.

## SmartMed Functional Review
Verify:
- today's medication is visible
- next medication is obvious
- dose is readable
- compartment information is clear
- device state is visible
- rotation state is visible
- READY is distinguishable
- TAKEN is distinguishable
- ERROR is understandable

## 3D Review
Verify:
- realistic physical appearance
- exactly 8 compartments
- 45-degree spacing
- entire tray rotates
- individual compartments do not rotate independently
- front access position is fixed
- target reaches access position
- camera stays stable during automatic rotation
- product has depth
- model does not look like a pie chart

## Visual Hierarchy Review
Check:
- page title
- primary action
- current medication
- next medication
- device state
- schedule
- inventory
- history

Ask: "What should the user notice first?" If unclear, improve hierarchy.

## Spacing Review
Look for:
- inconsistent padding
- inconsistent margins
- crowded cards
- excessive empty space
- misaligned elements
- inconsistent grid gaps

## Typography Review
Check:
- font consistency
- heading hierarchy
- body readability
- line height
- numeric readability
- Vietnamese characters

## Color Review
Use consistent semantics:
- Blue = primary
- Green = completed/safe
- Orange = upcoming
- Red = error
- Gray = inactive

Avoid random colors and excessive gradients.

## Accessibility Review
Check:
- text contrast
- keyboard focus
- button labels
- semantic HTML
- form labels
- touch target sizes
- information is not communicated only through color
- important medication states have text labels

## Responsive Review
Test:
- Desktop
- Laptop
- Tablet
- Mobile

Check sidebar, header, cards, 3D canvas, tables, navigation, and buttons.

Prevent:
- horizontal overflow
- clipped text
- clipped 3D device
- overlapping cards
- unreadable buttons

## Interaction Review
Check hover, active, focus, loading, success, error, and transition states.

Animations must be purposeful.

## Healthcare UX Review
Avoid:
- confusing terminology
- ambiguous status
- excessive decoration
- excessive flashing
- aggressive animations
- gaming-style effects

Medication information must remain the most important content.

## Error Review
Prefer:
"Không thể xoay khay thuốc. Kiểm tra nguồn điện hoặc vị trí khay."

Technical error codes may be secondary diagnostic information.

## Performance Review
Look for:
- unnecessary re-renders
- oversized assets
- excessive animation
- expensive 3D effects
- unnecessary dependencies
- duplicated components

## Code Quality Review
Check:
- reusable components
- clear naming
- separation of concerns
- centralized constants
- state management
- component size
- duplication

Do not rewrite working architecture without reason.

## Final Product Review
Ask:
- Does it look like a real product?
- Does it communicate the physical SmartMed mechanism?
- Can the user immediately understand the next medication?
- Can the user understand whether the device is ready?
- Does the 3D model reinforce the UI?
- Does it work on mobile?
- Is it accessible?
- Is it consistent?

## Improvement Strategy
When finding a problem:
1. Explain it briefly.
2. Identify affected component.
3. Make the smallest appropriate change.
4. Preserve existing functionality.
5. Re-check the affected area.
6. Check for regressions.

A polished interface is:
Purpose + Hierarchy + Consistency + Usability + Visual quality + Performance.
