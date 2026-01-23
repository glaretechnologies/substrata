
// Select all elements with the class 'delete-chatbot'
document.querySelectorAll('.delete-chatbot').forEach(function(element) {
	element.addEventListener('click', function(event) {
		// Show the confirmation dialog
		if (!confirm('Are you sure you want to delete this chatbot?')) {
			// Prevent the default action (form submit or link navigation) if canceled
			event.preventDefault();
		}
	});
});
