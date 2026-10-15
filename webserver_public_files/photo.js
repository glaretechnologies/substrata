
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


// Submit emoji reaction forms with fetch, and update the page in place, instead of reloading the page.
// The server replies with the new reaction summary HTML (built by reactionSummaryHTML() in PhotoHandlers.cpp, with user names HTML-escaped).
document.querySelectorAll('.photo-reaction-form').forEach(function(form) {
	form.addEventListener('submit', function(event) {
		event.preventDefault();

		const button = form.querySelector('.photo-reaction');
		if(button.disabled)
			return;
		button.disabled = true;

		const data = new FormData(form);
		data.append('ajax', '1');

		fetch(form.action, { method: 'POST', body: new URLSearchParams(data) })
			.then(function(response) { return response.json(); })
			.then(function(result) {
				if(result.error) {
					alert(result.error);
					return;
				}
				button.classList.toggle('reacted', result.reacted);
				document.querySelector('.photo-reaction-summary').innerHTML = result.summary_html;
			})
			.catch(function(err) {
				alert('Failed to send reaction: ' + err);
			})
			.finally(function() {
				button.disabled = false;
			});
	});
});
