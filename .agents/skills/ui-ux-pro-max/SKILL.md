---
name: ui-ux-pro-max
description: Provides advanced UI/UX design direction for SmartMed healthcare IoT interfaces, including design systems, visual hierarchy, user flows, color systems, typography, interaction design, responsive layouts, accessibility, and usability.
---

# UI/UX Pro Max Skill

## Objective
Design interfaces that are visually attractive while remaining easy to understand and operate.

For SmartMed, prioritize medication safety, clarity, low cognitive load, predictable interaction, clear device state, and readable medication information.

## Primary User Questions
The dashboard should quickly answer:
1. What medicine do I need to take?
2. When do I need to take it?
3. Has the device prepared it?
4. Which compartment is accessible?
5. Have I taken it?
6. How much remains?
7. Is the device working normally?

## Information Hierarchy
Use:
CURRENT MEDICATION
↓
NEXT MEDICATION
↓
DEVICE STATE
↓
TODAY'S SCHEDULE
↓
INVENTORY
↓
HISTORY
↓
SECONDARY INFORMATION

## Medication Card
Clearly show:
- medicine name
- dose
- scheduled time
- compartment
- status

Example:
Vitamin C
1 viên
20:00
Ngăn 5
Sắp uống

## Medication States
Use consistent labels:
- Sắp uống
- Đang chuẩn bị
- Sẵn sàng
- Đã uống
- Bỏ lỡ
- Lỗi

## Device State
Prefer human-readable states:
"Motor — Hoạt động bình thường"
rather than only "MOTOR: OK".

## Error UX
Explain:
1. What happened
2. Why it matters
3. What the user can do

Example:
"Không thể xoay khay thuốc. Hệ thống chưa nhận được tín hiệu vị trí từ cảm biến. Kiểm tra thiết bị."

## Visual Hierarchy
Use size, weight, spacing, contrast, and position. Do not rely only on color.

## Healthcare Color Semantics
- Blue = primary/active
- Green = completed/safe
- Orange = upcoming/attention
- Red = error/missed
- Gray = inactive/unavailable

## Design System
Centralize design tokens when possible:
```javascript
const designTokens = {
  radius: { sm: 8, md: 12, lg: 20 },
  spacing: { sm: 8, md: 16, lg: 24, xl: 32 }
};
```
Adapt to the existing project.

## User Flow
Scheduled
→ Upcoming
→ Device rotates
→ Ready
→ User takes medicine
→ Taken

The UI should mirror this real-world process.

## 3D Interaction
The 3D device should answer:
- Where is the medicine?
- Which compartment is accessible?
- Is the tray moving?
- Is it ready?

Do not require manual 3D exploration to understand medication position.

## Responsive UX
Mobile must preserve:
- next medicine
- time
- dose
- status
- device readiness

Secondary information can collapse.

## Empty States
Use intentional messages, e.g.:
"Không có lịch uống thuốc hôm nay. Bạn có thể thêm lịch uống mới từ mục Lịch uống."

## Loading States
Use meaningful labels such as:
"Đang tải mô hình thiết bị..."
"Đang tải lịch uống..."

## UX Review Questions
- Can the user identify the next medicine quickly?
- Is the current device state obvious?
- Is READY distinct from ROTATING?
- Is medication-taking confirmation obvious?
- Are errors understandable?
- Does the 3D model explain the mechanism?
- Is mobile use practical?

Improve UX before adding decoration.
