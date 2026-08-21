(() => {
  'use strict';

  // Matrix rain background
  const canvas = document.getElementById('matrix');
  const ctx = canvas.getContext('2d');
  let width, height;

  function resizeCanvas() {
    width = window.innerWidth;
    height = window.innerHeight;
    canvas.width = width;
    canvas.height = height;
  }

  resizeCanvas();
  window.addEventListener('resize', resizeCanvas);

  const cols = Math.floor(width / 16) + 1;
  const drops = Array(cols).fill(1);
  const chars = '01SCHOOL21CTF{s3cur3}';

  function drawMatrix() {
    ctx.fillStyle = 'rgba(7, 11, 20, 0.07)';
    ctx.fillRect(0, 0, width, height);
    ctx.font = '14px JetBrains Mono';

    for (let i = 0; i < drops.length; i++) {
      const char = chars[Math.floor(Math.random() * chars.length)];
      const x = i * 16;
      const y = drops[i] * 16;
      ctx.fillStyle = i % 5 === 0 ? '#00ff9d' : '#007f4f';
      ctx.fillText(char, x, y);

      if (y > height && Math.random() > 0.975) {
        drops[i] = 0;
      }
      drops[i]++;
    }
  }

  setInterval(drawMatrix, 45);

  // Typewriter effect
  const title = 'School 21 CTF';
  const typedEl = document.getElementById('typed');
  let charIndex = 0;

  function type() {
    if (charIndex <= title.length) {
      typedEl.textContent = title.slice(0, charIndex);
      charIndex++;
      setTimeout(type, 120);
    } else {
      setTimeout(() => {
        charIndex = 0;
        type();
      }, 2500);
    }
  }
  type();

  // Countdown to next Saturday 11:00 UTC
  const now = new Date();
  const eventDate = new Date();
  eventDate.setUTCDate(now.getUTCDate() + ((6 + 7 - now.getUTCDay()) % 7 || 7));
  eventDate.setUTCHours(11, 0, 0, 0);

  if (eventDate - now < 0) {
    eventDate.setUTCDate(eventDate.getUTCDate() + 7);
  }

  function updateCountdown() {
    const diff = eventDate - new Date();

    if (diff <= 0) {
      document.getElementById('days').textContent = '00';
      document.getElementById('hours').textContent = '00';
      document.getElementById('minutes').textContent = '00';
      document.getElementById('seconds').textContent = '00';
      return;
    }

    const days = Math.floor(diff / (1000 * 60 * 60 * 24));
    const hours = Math.floor((diff / (1000 * 60 * 60)) % 24);
    const minutes = Math.floor((diff / (1000 * 60)) % 60);
    const seconds = Math.floor((diff / 1000) % 60);

    document.getElementById('days').textContent = String(days).padStart(2, '0');
    document.getElementById('hours').textContent = String(hours).padStart(2, '0');
    document.getElementById('minutes').textContent = String(minutes).padStart(2, '0');
    document.getElementById('seconds').textContent = String(seconds).padStart(2, '0');
  }

  updateCountdown();
  setInterval(updateCountdown, 1000);

  // Registration form
  const form = document.getElementById('register-form');
  const status = document.getElementById('form-status');

  form.addEventListener('submit', (e) => {
    e.preventDefault();
    const email = form.email.value.trim();

    if (!email || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
      status.textContent = 'Please enter a valid email.';
      status.style.color = 'var(--danger)';
      return;
    }

    status.style.color = 'var(--accent)';
    status.textContent = 'You are registered! Check your inbox soon.';
    form.reset();
  });
})();
