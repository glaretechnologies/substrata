// Dark mode functionality
(function() {
	'use strict';
	
	// Check for saved user preference, otherwise check system preference
	const savedTheme = localStorage.getItem('theme');
	const systemPrefersDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
	
	// Apply initial theme
	if (savedTheme === 'dark' || (!savedTheme && systemPrefersDark)) {
		document.body.classList.add('dark-mode');
	}
	
	// Toggle dark mode function
	window.toggleDarkMode = function() {
		document.body.classList.toggle('dark-mode');
		
		// Save preference to localStorage
		const isDarkMode = document.body.classList.contains('dark-mode');
		localStorage.setItem('theme', isDarkMode ? 'dark' : 'light');
	};
	
	// Listen for system theme changes
	if (window.matchMedia) {
		window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', function(e) {
			// Only auto-switch if user hasn't manually set a preference
			if (!localStorage.getItem('theme')) {
				if (e.matches) {
					document.body.classList.add('dark-mode');
				} else {
					document.body.classList.remove('dark-mode');
				}
			}
		});
	}
})();
