
// Select all elements with the class 'delete-photo'
document.querySelectorAll('.delete-photo').forEach(function(element) {
	element.addEventListener('click', function(event) {
		// Show the confirmation dialog
		if (!confirm('Are you sure you want to delete this photo?')) {
			// Prevent the default action (form submit or link navigation) if canceled
			event.preventDefault();
		}
	});
});
