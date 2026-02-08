
// Code for fading in and out header images.
// Adapted from https://stackoverflow.com/a/25348073

// Read image paths from 'top-image-paths' element in root_page.htmlfrag
var imgArray = document.getElementById("top-image-paths").textContent.split(',').map(s => s.trim());

if(imgArray.length > 1) {
	var curIndex = 0;
	var imgDuration = 7000;

	function transitionImage() {
		curIndex++;
		if (curIndex == imgArray.length) { curIndex = 0; }

		document.getElementById('top-image').className = "top-image fadeOut"; // Start fading out by adding fadeOut class 
		setTimeout(function () { // Once we have faded out, change to next image.
			document.getElementById('top-image').src = imgArray[curIndex]; // Change to next image
			document.getElementById('top-image').className = "top-image"; // Remove fadeOut class
		}, 2000); // NOTE: this timeout value should match the time of the transition setting in #top-image in main.css.
	   
		setTimeout(transitionImage, imgDuration);
	}

	setTimeout(transitionImage, imgDuration - 2000);
}
