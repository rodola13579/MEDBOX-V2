/**
 * carousel2d.js — SmartMed Interactive 2D Circular Dispenser
 * 
 * Mechanical Architecture:
 * - 8 distinct color-coded radial compartments (Slots 0..7).
 * - Slot 0: Safe Home / Protective lid (Khóa an toàn).
 * - Stationary Housing with FIXED Access Window at 6 o'clock (Hướng 6 giờ về phía người dùng).
 * - The cover does NOT move; the internal tray rotates so the requested slot aligns with 6 o'clock.
 * - Smooth shortest-path rotation with cubic easing.
 * - Multi-state glowing LED indicator at the 6 o'clock aperture.
 */

const SMARTMED_SLOT_COLORS = [
  {
    slot: 0,
    name: 'Safe Home',
    colorName: 'Khóa An Toàn (Safe Home)',
    primaryColor: '#334155', // Slate dark
    gradientStart: '#475569',
    gradientEnd: '#1e293b',
    accentColor: '#10B981', // Safe green accent
    textColor: '#F8FAFC',
    icon: '🔒',
  },
  {
    slot: 1,
    name: 'Ngăn 1',
    colorName: 'Xanh Đại Dương',
    primaryColor: '#2563EB', // Blue
    gradientStart: '#3B82F6',
    gradientEnd: '#1D4ED8',
    accentColor: '#93C5FD',
    textColor: '#FFFFFF',
    icon: '💊',
  },
  {
    slot: 2,
    name: 'Ngăn 2',
    colorName: 'Xanh Ngọc Lục Bảo',
    primaryColor: '#059669', // Emerald
    gradientStart: '#10B981',
    gradientEnd: '#047857',
    accentColor: '#6EE7B7',
    textColor: '#FFFFFF',
    icon: '💊',
  },
  {
    slot: 3,
    name: 'Ngăn 3',
    colorName: 'Vàng Cam Hổ Phách',
    primaryColor: '#D97706', // Amber
    gradientStart: '#F59E0B',
    gradientEnd: '#B45309',
    accentColor: '#FDE68A',
    textColor: '#FFFFFF',
    icon: '💊',
  },
  {
    slot: 4,
    name: 'Ngăn 4',
    colorName: 'Tím Thạch Anh',
    primaryColor: '#7C3AED', // Purple
    gradientStart: '#8B5CF6',
    gradientEnd: '#6D28D9',
    accentColor: '#DDD6FE',
    textColor: '#FFFFFF',
    icon: '💊',
  },
  {
    slot: 5,
    name: 'Ngăn 5',
    colorName: 'Hồng San Hô',
    primaryColor: '#E11D48', // Rose
    gradientStart: '#F43F5E',
    gradientEnd: '#BE123C',
    accentColor: '#FECDD3',
    textColor: '#FFFFFF',
    icon: '💊',
  },
  {
    slot: 6,
    name: 'Ngăn 6',
    colorName: 'Xanh Lơ Aqua',
    primaryColor: '#0891B2', // Cyan
    gradientStart: '#06B6D4',
    gradientEnd: '#0E7490',
    accentColor: '#A5F3FC',
    textColor: '#FFFFFF',
    icon: '💊',
  },
  {
    slot: 7,
    name: 'Ngăn 7',
    colorName: 'Xanh Chàm Indigo',
    primaryColor: '#4F46E5', // Indigo
    gradientStart: '#6366F1',
    gradientEnd: '#4338CA',
    accentColor: '#C7D2FE',
    textColor: '#FFFFFF',
    icon: '💊',
  },
];

class SmartMedCarousel2D {
  constructor(containerId) {
    this.container = document.getElementById(containerId);
    if (!this.container) {
      console.warn(`[Carousel2D] Container #${containerId} không tồn tại.`);
      return;
    }

    this.currentSlot = 0;
    this.currentAngle = 0; // degrees
    this.targetAngle = 0;
    this.isAnimating = false;
    this.animationStartTime = 0;
    this.animationDuration = 1000;
    this.startAngle = 0;

    this.ledState = 'green'; // 'green', 'blue', 'amber', 'red'
    this.slotsData = [];

    // SVG Geometry Constants
    this.cx = 220;
    this.cy = 220;
    this.outerRadius = 172;
    this.innerRadius = 46;

    this.init();
  }

  init() {
    this.container.innerHTML = '';
    this.container.classList.add('dispenser-2d-wrapper');

    // Build the SVG structure
    const svgHTML = `
      <svg id="dispenser-svg-root" viewBox="0 0 440 470" class="dispenser-svg" xmlns="http://www.w3.org/2000/svg">
        <defs>
          <!-- Radial Shading Filters & Shadows -->
          <filter id="dispenser-shadow" x="-10%" y="-10%" width="130%" height="130%">
            <feDropShadow dx="0" dy="6" stdDeviation="8" flood-color="#0f172a" flood-opacity="0.18" />
          </filter>
          <filter id="slot-glow" x="-20%" y="-20%" width="140%" height="140%">
            <feGaussianBlur stdDeviation="3" result="blur" />
            <feComposite in="SourceGraphic" in2="blur" operator="over" />
          </filter>
          <filter id="led-aperture-glow" x="-30%" y="-30%" width="160%" height="160%">
            <feGaussianBlur stdDeviation="4" result="blur" />
            <feComposite in="SourceGraphic" in2="blur" operator="over" />
          </filter>

          <!-- Gradients for all 8 sectors -->
          ${SMARTMED_SLOT_COLORS.map(
            (c) => `
            <radialGradient id="grad-slot-${c.slot}" cx="50%" cy="50%" r="50%" fx="30%" fy="30%">
              <stop offset="0%" stop-color="${c.gradientStart}" />
              <stop offset="100%" stop-color="${c.gradientEnd}" />
            </radialGradient>
          `
          ).join('')}

          <!-- Outer Metallic Ring Gradient -->
          <linearGradient id="metallic-rim" x1="0%" y1="0%" x2="100%" y2="100%">
            <stop offset="0%" stop-color="#F8FAFC" />
            <stop offset="25%" stop-color="#CBD5E1" />
            <stop offset="50%" stop-color="#FFFFFF" />
            <stop offset="75%" stop-color="#94A3B8" />
            <stop offset="100%" stop-color="#E2E8F0" />
          </linearGradient>

          <!-- Central Hub Gradient -->
          <radialGradient id="hub-gradient" cx="40%" cy="40%" r="60%">
            <stop offset="0%" stop-color="#334155" />
            <stop offset="60%" stop-color="#1E293B" />
            <stop offset="100%" stop-color="#0F172A" />
          </radialGradient>
        </defs>

        <!-- STATIONARY OUTER HOUSING & BASE -->
        <g id="stationary-base" filter="url(#dispenser-shadow)">
          <!-- Outer Chassis Ring -->
          <circle cx="${this.cx}" cy="${this.cy}" r="196" fill="url(#metallic-rim)" stroke="#94A3B8" stroke-width="2" />
          <!-- Inner Rim Bezel -->
          <circle cx="${this.cx}" cy="${this.cy}" r="182" fill="#0F172A" stroke="#334155" stroke-width="1.5" />
          <!-- Track groove -->
          <circle cx="${this.cx}" cy="${this.cy}" r="176" fill="#1E293B" stroke="#0F172A" stroke-width="2" />
        </g>

        <!-- ROTATING TRAY (CHỨA 8 NGĂN KHAY XOAY QUANH TRỤC TRUNG TÂM) -->
        <g id="rotating-disc" transform="rotate(0 ${this.cx} ${this.cy})">
          <!-- 8 Radial Compartments -->
          <g id="sectors-group">
            ${this._generateSectorsHTML()}
          </g>

          <!-- Divider Ridge Lines -->
          <g id="dividers-group">
            ${this._generateDividersHTML()}
          </g>
        </g>

        <!-- CENTRAL AXIS HUB (TRỤC QUAY TRUNG TÂM) -->
        <g id="center-hub">
          <circle cx="${this.cx}" cy="${this.cy}" r="${this.innerRadius + 4}" fill="#0F172A" />
          <circle cx="${this.cx}" cy="${this.cy}" r="${this.innerRadius}" fill="url(#hub-gradient)" stroke="#64748B" stroke-width="2" />
          <circle cx="${this.cx}" cy="${this.cy}" r="28" fill="#0F172A" stroke="#38BDF8" stroke-width="1.5" stroke-dasharray="3 2" />
          <!-- SmartMed Brand Monogram -->
          <text x="${this.cx}" y="${this.cy - 5}" text-anchor="middle" font-size="10" font-weight="800" fill="#38BDF8" letter-spacing="1">
            SMART
          </text>
          <text x="${this.cx}" y="${this.cy + 9}" text-anchor="middle" font-size="9" font-weight="800" fill="#FFFFFF" letter-spacing="1">
            MED
          </text>
          <circle cx="${this.cx}" cy="${this.cy + 20}" r="3" fill="#10B981" id="hub-power-dot" />
        </g>

        <!-- STATIONARY 6 O'CLOCK FIXED APERTURE (CỬA LẤY & NẠP THUỐC CỐ ĐỊNH HƯỚNG 6 GIỜ) -->
        <g id="fixed-aperture-6oclock">
          <!-- Aperture Gate Frame at 6 o'clock -->
          <path id="aperture-glow-ring"
                d="${this._generateApertureArcPath()}"
                fill="none"
                stroke="#10B981"
                stroke-width="7"
                stroke-linecap="round"
                filter="url(#led-aperture-glow)" />

          <!-- Target Guidance Bracket / Gate Reticle -->
          <path d="M 175 390 L 175 402 L 195 402" fill="none" stroke="#FFFFFF" stroke-width="2.5" stroke-linecap="round" />
          <path d="M 265 390 L 265 402 L 245 402" fill="none" stroke="#FFFFFF" stroke-width="2.5" stroke-linecap="round" />

          <!-- 6 o'clock Aperture Pointer Pin -->
          <polygon points="220,388 212,400 228,400" fill="#10B981" id="aperture-arrow-head" />

          <!-- Aperture Label Box (Hướng 6 giờ) -->
          <g transform="translate(220, 435)">
            <rect x="-140" y="-18" width="280" height="34" rx="17" fill="#0F172A" stroke="#334155" stroke-width="1.5" />
            <circle cx="-118" cy="-1" r="5" fill="#10B981" id="aperture-status-dot" />
            <text x="-106" y="4" font-size="11" font-weight="700" fill="#F8FAFC" text-anchor="start">
              📍 CỬA NẠP & LẤY THUỐC (HƯỚNG 6 GIỜ)
            </text>
          </g>
        </g>
      </svg>
    `;

    this.container.innerHTML = svgHTML;
    this.rotatingDisc = document.getElementById('rotating-disc');

    // Setup interactive clicks on sectors
    this._setupSectorClicks();

    // Start with slot 0
    this.rotateToSlot(0, 0);
  }

  /**
   * Generates SVG path definition for an annular sector wedge.
   * Angles in degrees where 90 deg is 6 o'clock (down).
   */
  _generateSectorPath(startAngleDeg, endAngleDeg) {
    const toRad = (deg) => (deg * Math.PI) / 180;
    const rad1 = toRad(startAngleDeg);
    const rad2 = toRad(endAngleDeg);

    // Outer arc points
    const x1 = this.cx + this.outerRadius * Math.cos(rad1);
    const y1 = this.cy + this.outerRadius * Math.sin(rad1);
    const x2 = this.cx + this.outerRadius * Math.cos(rad2);
    const y2 = this.cy + this.outerRadius * Math.sin(rad2);

    // Inner arc points
    const x3 = this.cx + this.innerRadius * Math.cos(rad2);
    const y3 = this.cy + this.innerRadius * Math.sin(rad2);
    const x4 = this.cx + this.innerRadius * Math.cos(rad1);
    const y4 = this.cy + this.innerRadius * Math.sin(rad1);

    return `M ${x1} ${y1} A ${this.outerRadius} ${this.outerRadius} 0 0 1 ${x2} ${y2} L ${x3} ${y3} A ${this.innerRadius} ${this.innerRadius} 0 0 0 ${x4} ${y4} Z`;
  }

  _generateApertureArcPath() {
    const toRad = (deg) => (deg * Math.PI) / 180;
    const r = this.outerRadius + 8;
    const a1 = toRad(90 - 24); // 66 deg
    const a2 = toRad(90 + 24); // 114 deg
    const x1 = this.cx + r * Math.cos(a1);
    const y1 = this.cy + r * Math.sin(a1);
    const x2 = this.cx + r * Math.cos(a2);
    const y2 = this.cy + r * Math.sin(a2);
    return `M ${x1} ${y1} A ${r} ${r} 0 0 1 ${x2} ${y2}`;
  }

  _generateSectorsHTML() {
    return SMARTMED_SLOT_COLORS.map((cfg) => {
      const slot = cfg.slot;
      // Slot k center is naturally at 90 + k * 45 degrees
      const centerAngle = 90 + slot * 45;
      const startAngle = centerAngle - 22.5;
      const endAngle = centerAngle + 22.5;
      const pathD = this._generateSectorPath(startAngle, endAngle);

      // Mid radius for label placing
      const midR = (this.innerRadius + this.outerRadius) / 2 + 6;
      const toRad = (deg) => (deg * Math.PI) / 180;
      const midRad = toRad(centerAngle);
      const labelX = this.cx + midR * Math.cos(midRad);
      const labelY = this.cy + midR * Math.sin(midRad);

      // Badge position closer to center
      const badgeR = this.innerRadius + 22;
      const badgeX = this.cx + badgeR * Math.cos(midRad);
      const badgeY = this.cy + badgeR * Math.sin(midRad);

      const isSafeHome = slot === 0;

      return `
        <g id="sector-${slot}" class="dispenser-sector ${isSafeHome ? 'safe-home' : ''}" data-slot="${slot}" style="cursor:pointer;">
          <!-- Sector Wedge -->
          <path d="${pathD}"
                fill="url(#grad-slot-${slot})"
                stroke="#0F172A"
                stroke-width="2.5"
                class="sector-wedge" />

          <!-- Subtle Inner highlight arc -->
          <path d="${this._generateSectorPath(startAngle + 2, endAngle - 2)}"
                fill="none"
                stroke="rgba(255,255,255,0.18)"
                stroke-width="1.5"
                pointer-events="none" />

          <!-- Compartment Number Badge Circle -->
          <circle cx="${badgeX}" cy="${badgeY}" r="13" fill="#0F172A" stroke="${cfg.accentColor}" stroke-width="2" />
          <text x="${badgeX}" y="${badgeY + 4}" text-anchor="middle" font-size="11" font-weight="800" fill="${cfg.accentColor}">
            ${isSafeHome ? '0' : slot}
          </text>

          <!-- Sector Medicine Info -->
          <g transform="translate(${labelX}, ${labelY})" pointer-events="none">
            ${
              isSafeHome
                ? `
                <text x="0" y="-4" text-anchor="middle" font-size="10" font-weight="800" fill="#F8FAFC">
                  SAFE HOME
                </text>
                <text x="0" y="9" text-anchor="middle" font-size="8.5" font-weight="700" fill="#10B981">
                  🔒 Khóa An Toàn
                </text>
              `
                : `
                <text id="sector-med-name-${slot}" x="0" y="-5" text-anchor="middle" font-size="9.5" font-weight="700" fill="#FFFFFF">
                  Ngăn ${slot}
                </text>
                <text id="sector-pill-count-${slot}" x="0" y="8" text-anchor="middle" font-size="8.5" font-weight="600" fill="${cfg.accentColor}">
                  0 viên
                </text>
              `
            }
          </g>
        </g>
      `;
    }).join('');
  }

  _generateDividersHTML() {
    const toRad = (deg) => (deg * Math.PI) / 180;
    return [0, 1, 2, 3, 4, 5, 6, 7]
      .map((i) => {
        const angle = 90 + i * 45 - 22.5;
        const rad = toRad(angle);
        const x1 = this.cx + this.innerRadius * Math.cos(rad);
        const y1 = this.cy + this.innerRadius * Math.sin(rad);
        const x2 = this.cx + this.outerRadius * Math.cos(rad);
        const y2 = this.cy + this.outerRadius * Math.sin(rad);
        return `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="#0F172A" stroke-width="3" stroke-linecap="round" />`;
      })
      .join('');
  }

  _setupSectorClicks() {
    for (let slot = 0; slot <= 7; slot++) {
      const el = document.getElementById(`sector-${slot}`);
      if (el) {
        el.addEventListener('click', () => {
          this.rotateToSlot(slot);
          if (slot > 0 && typeof window.openRefillModal === 'function') {
            window.openRefillModal(slot);
          } else if (slot === 0 && typeof window.cancelInlineTask === 'function') {
            window.cancelInlineTask();
          }
        });
      }
    }
  }

  /**
   * Rotates the entire tray to bring Slot slotIndex to the 6 o'clock position.
   * To bring Slot k to 6 o'clock (natural angle = 90 + k*45), we rotate by:
   * targetAngle = -k * 45 degrees.
   */
  rotateToSlot(slotIndex, customDuration = 1000) {
    if (slotIndex < 0 || slotIndex > 7) return;

    this.currentSlot = slotIndex;
    const baseTarget = -slotIndex * 45;

    // Choose shortest angular path from currentAngle
    const currentNorm = this.currentAngle % 360;
    let delta = (baseTarget - currentNorm) % 360;
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;

    this.targetAngle = this.currentAngle + delta;
    this.startAngle = this.currentAngle;

    if (customDuration === 0) {
      this.currentAngle = this.targetAngle;
      if (this.rotatingDisc) {
        this.rotatingDisc.setAttribute('transform', `rotate(${this.currentAngle} ${this.cx} ${this.cy})`);
      }
      this._updateActiveSectorStyle(slotIndex);
      return;
    }

    this.isAnimating = true;
    this.animationStartTime = performance.now();
    this.animationDuration = customDuration;

    // Set LED state based on target slot
    if (slotIndex === 0) {
      this.setLEDState('green');
    } else {
      this.setLEDState('blue');
    }

    // Update banner badge
    const badgeEl = document.getElementById('current-slot-badge');
    if (badgeEl) {
      const cfg = SMARTMED_SLOT_COLORS[slotIndex];
      if (slotIndex === 0) {
        badgeEl.innerHTML = `🔒 <strong>Ngăn 0: Safe Home</strong> (Khóa nắp che an toàn tại cửa 6h)`;
        badgeEl.className = 'canvas-badge safe-home-active';
      } else {
        const slotData = this.slotsData.find((s) => s.slot_number === slotIndex);
        const medName = slotData?.medicine?.name || cfg.name;
        badgeEl.innerHTML = `🚪 <strong>Ngăn ${slotIndex} (${medName})</strong> — Đang mở tại cửa 6 giờ`;
        badgeEl.className = 'canvas-badge door-open';
      }
    }

    this._animateRotation();
  }

  _animateRotation() {
    const step = (time) => {
      if (!this.isAnimating) return;

      const elapsed = time - this.animationStartTime;
      const progress = Math.min(elapsed / this.animationDuration, 1);

      // Smooth Easing: cubic-bezier-like easeInOutCubic
      const ease = progress < 0.5 ? 4 * progress * progress * progress : 1 - Math.pow(-2 * progress + 2, 3) / 2;

      this.currentAngle = this.startAngle + (this.targetAngle - this.startAngle) * ease;

      if (this.rotatingDisc) {
        this.rotatingDisc.setAttribute('transform', `rotate(${this.currentAngle} ${this.cx} ${this.cy})`);
      }

      if (progress >= 1) {
        this.isAnimating = false;
        this.currentAngle = this.targetAngle;
        if (this.rotatingDisc) {
          this.rotatingDisc.setAttribute('transform', `rotate(${this.currentAngle} ${this.cx} ${this.cy})`);
        }
        this._updateActiveSectorStyle(this.currentSlot);
      } else {
        requestAnimationFrame(step);
      }
    };

    requestAnimationFrame(step);
  }

  _updateActiveSectorStyle(activeSlot) {
    for (let slot = 0; slot <= 7; slot++) {
      const el = document.getElementById(`sector-${slot}`);
      if (el) {
        if (slot === activeSlot) {
          el.classList.add('active-at-aperture');
        } else {
          el.classList.remove('active-at-aperture');
        }
      }
    }
  }

  /**
   * Updates slot labels, medicine names, and pill counts on the 2D sectors.
   */
  updateSlotsData(slotsData) {
    if (!Array.isArray(slotsData)) return;
    this.slotsData = slotsData;

    slotsData.forEach((slot) => {
      const slotNum = slot.slot_number;
      if (slotNum >= 1 && slotNum <= 7) {
        const nameEl = document.getElementById(`sector-med-name-${slotNum}`);
        const countEl = document.getElementById(`sector-pill-count-${slotNum}`);

        const medName = slot.medicine?.name;
        const qty = slot.current_quantity || 0;

        if (nameEl) {
          if (medName) {
            // Truncate if too long to fit cleanly in sector
            nameEl.textContent = medName.length > 9 ? medName.substring(0, 8) + '…' : medName;
          } else {
            nameEl.textContent = `Ngăn ${slotNum}`;
          }
        }

        if (countEl) {
          countEl.textContent = qty > 0 ? `${qty} viên` : 'Trống';
        }
      }
    });

    // Re-verify active slot style
    this._updateActiveSectorStyle(this.currentSlot);
  }

  /**
   * Sets glowing LED state on the 6 o'clock fixed aperture.
   * State: 'green' | 'blue' | 'amber' | 'red'
   */
  setLEDState(stateName) {
    this.ledState = stateName;

    const colors = {
      green: '#10B981', // Safe Home / Ready
      blue: '#3B82F6',  // Active dispensing
      amber: '#F59E0B', // Warning / low stock
      red: '#EF4444',   // Jammed / critical error
    };

    const activeColor = colors[stateName] || colors.green;

    const ring = document.getElementById('aperture-glow-ring');
    const arrow = document.getElementById('aperture-arrow-head');
    const dot = document.getElementById('aperture-status-dot');
    const hubDot = document.getElementById('hub-power-dot');

    if (ring) ring.setAttribute('stroke', activeColor);
    if (arrow) arrow.setAttribute('fill', activeColor);
    if (dot) dot.setAttribute('fill', activeColor);
    if (hubDot) hubDot.setAttribute('fill', activeColor);

    // Also update bottom indicator dot in HTML if present
    const extDot = document.getElementById('led-indicator-dot');
    const extText = document.getElementById('led-indicator-text');
    if (extDot) extDot.style.background = activeColor;
    if (extText) {
      if (stateName === 'green') extText.textContent = 'LED Vòng: An toàn (Ngăn 0)';
      else if (stateName === 'blue') extText.textContent = 'LED Vòng: Đang hoạt động';
      else if (stateName === 'amber') extText.textContent = 'LED Vòng: Cảnh báo';
      else if (stateName === 'red') extText.textContent = 'LED Vòng: Lỗi kẹt cơ';
    }
  }

  onResize() {
    // SVG scales automatically with viewBox, nothing needed
  }
}

// Export globally
window.SmartMedCarousel2D = SmartMedCarousel2D;
window.SMARTMED_SLOT_COLORS = SMARTMED_SLOT_COLORS;
