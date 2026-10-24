---
name: frontend-design
description: Creates polished, production-quality frontend interfaces for SmartMed and other modern web applications. Use when designing dashboard layouts, components, navigation, cards, forms, responsive interfaces, typography, spacing, visual hierarchy, or frontend styling.
---

# Frontend Design Skill

## Goal
Create frontend interfaces that look intentionally designed by a professional product designer. Avoid generic AI-generated dashboard aesthetics.

## Design Principles
Prioritize:
1. Clarity
2. Hierarchy
3. Consistency
4. Accessibility
5. Responsiveness
6. Visual polish
7. Performance

Do not add visual elements simply because empty space exists.

## SmartMed Visual Direction
SmartMed is a healthcare IoT product. The interface should feel:
- calm
- trustworthy
- modern
- premium
- friendly
- technological
- clean

Avoid gaming aesthetics, hacker/cyberpunk styling, excessive neon, excessive gradients, overly dark UI, and excessive glassmorphism.

## Layout
Prefer:
Sidebar → Main content → Dashboard sections

Typical navigation:
- Tổng quan
- Thuốc
- Lịch uống
- Lịch sử
- Kho thuốc
- Thiết bị
- Cài đặt

Do not overcrowd the dashboard.

## Typography
Use a modern sans-serif font with excellent Vietnamese support.

Use a clear hierarchy:
- Page title
- Section title
- Card title
- Body
- Secondary information
- Metadata

Do not use too many font sizes.

## Spacing
Prefer a consistent scale such as:
4, 8, 12, 16, 24, 32, 48

## Cards
Use:
- clear hierarchy
- moderate border radius
- subtle border
- subtle shadow
- sufficient internal spacing

Do not make every component a floating card.

## Buttons
Primary actions must be visually obvious. Examples:
- Uống thuốc
- Xem lịch
- Xoay đến ngăn
- Xác nhận
- Thêm thuốc

Do not create many competing primary buttons.

## Color System
Use restrained healthcare semantics:
- Primary: healthcare blue
- Success: soft green
- Warning: soft orange
- Error: soft red
- Neutral: white/light gray/blue-gray

Color must communicate meaning.

## Responsive Design
Desktop:
- sidebar
- multi-column dashboard
- large 3D visualization

Mobile:
- compact header
- stacked content
- reduced 3D canvas
- bottom navigation if appropriate

Do not simply shrink desktop.

## Micro-interactions
Use subtle hover, focus, active, loading, success, error, and transition states. Animation must support understanding.

## Accessibility
Ensure:
- sufficient contrast
- keyboard accessibility
- visible focus
- semantic HTML
- accessible labels
- meaningful button text
- no critical information communicated only by color

## Component Strategy
Prefer reusable components such as:
- DashboardCard
- StatusBadge
- MedicationCard
- ScheduleItem
- DeviceStatus
- InventoryCard
- HistoryTable
- Sidebar
- Header
- MedicineBox3D

Avoid huge monolithic components.

## Existing Project Rule
Before creating components:
1. Inspect the existing project.
2. Identify framework.
3. Identify styling system.
4. Identify component library.
5. Reuse existing conventions.
6. Avoid unnecessary dependencies.

Do not replace project architecture just to implement a visual change.

## Final Review
Check:
- layout hierarchy
- typography
- spacing
- semantic colors
- primary actions
- mobile behavior
- accessibility
- visual noise
- architecture preservation
- healthcare product feel
