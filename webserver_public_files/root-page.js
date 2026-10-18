
// Code for fading in and out header images.
// Adapted from https://stackoverflow.com/a/25348073

// Read image data from the 'top-images-data' element, written by MainPageHandlers.cpp.  Each child span has data-src, data-link and data-credit attributes.
var imgArray = Array.from(document.querySelectorAll('#top-images-data span')).map(e => e.dataset);

if(imgArray.length > 1) {
	var curIndex = 0;
	var imgDuration = 7000;

	var topImage = document.getElementById('top-image');
	var topImageCredit = document.getElementById('top-image-credit');

	// Start loading the next image before it's shown, so it's ready when the fade happens.
	function preloadNextImage() {
		var preload = new Image();
		preload.src = imgArray[(curIndex + 1) % imgArray.length].src;
	}

	function transitionImage() {
		curIndex++;
		if (curIndex == imgArray.length) { curIndex = 0; }

		topImage.className = "top-image fadeOut"; // Start fading out by adding fadeOut class
		topImageCredit.className = "top-image-credit fadeOut";
		setTimeout(function () { // Once we have faded out, change to next image.
			topImage.src = imgArray[curIndex].src; // Change to next image
			topImageCredit.href = imgArray[curIndex].link;
			topImageCredit.textContent = imgArray[curIndex].credit;
			topImage.className = "top-image"; // Remove fadeOut class
			topImageCredit.className = "top-image-credit";
			preloadNextImage();
		}, 2000); // NOTE: this timeout value should match the time of the transition setting in #top-image in main.css.

		setTimeout(transitionImage, imgDuration);
	}

	preloadNextImage();
	setTimeout(transitionImage, imgDuration - 2000);
}
