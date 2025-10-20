/*=====================================================================
URLWhitelist.cpp
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "URLWhitelist.h"


#include <StringUtils.h>
#include <algorithm>
#include <cassert>


URLWhitelist::URLWhitelist()
{}


void URLWhitelist::loadDefaultWhitelist()
{
	// Should have no protocol scheme, should have at least a slash for a path (important!), not just a raw domain
	const char* prefix_list[] = {
		// Video streaming
		"www.youtube.com/",
		"youtu.be/", // shortened version of youtube.com
		"vimeo.com/",
		"www.twitch.tv/",
		"twitch.tv/", // Redirects to www.twitch.tv, but some people are using this directly.

		// Audio streaming
		"open.spotify.com/",
		"soundcloud.com/",

		// Social media
		"www.facebook.com/",
		"twitter.com/",
		"x.com/",
		"www.reddit.com/",
		"news.ycombinator.com/",

		// Crypto
		"www.coinbase.com/",
		"www.coindesk.com/",
		"coinmarketcap.com/",

		// NFTs
		"opensea.io/",
		"rarible.com/",
		"superrare.com/",
		"foundation.app/",
		"niftygateway.com/",
		"mintable.app/",
		"www.binance.com/",
		"looksrare.org/",

		// Misc
		"substrata.info/",
		"nonfungibletc.com/",
		"codyellingham.com/",
		"www.codyellingham.com/",
		"www.cryptovoxels.com/",
		"forwardscattering.org/",
		"github.com/",
		"www.tempeltuttle.com/",
		"www.shadertoy.com/",
		"www.mindfly.art/",
		"html5test.com/",
		"www.google.com/",

		NULL
	};

	for(size_t i=0; i < staticArrayNumElems(prefix_list); ++i)
	{
		const char* prefix = prefix_list[i];
		if(prefix)
		{
			assert(StringUtils::containsChar(prefix, '/'));
			prefixes.push_back(prefix);
		}
	}

	std::sort(prefixes.begin(), prefixes.end());
}


/*
Since domain names are written with the top level domain on the right, we need to check the whole domain matches to avoid URLs like
"youtube.com.evil.site", which matches the prefix "youtube.com".

We will do this by always having at least one slash in the path part of the URL,
and having trailing slashes on any raw domain prefixes. (e.g. we will store "youtube.com/" instead of "youtube.com")
*/
bool URLWhitelist::isURLPrefixInWhitelist(const std::string& URL_) const
{
	std::string URL = URL_;
	
	// Remove protocol scheme if present.
	const size_t scheme_terminator_pos = URL.find("://");
	if(scheme_terminator_pos != std::string::npos)
	{
		URL = URL.substr(scheme_terminator_pos + 3);
	}

	// Append trailing slash if not present.
	if(!StringUtils::containsChar(URL, '/'))
		URL.push_back('/');

	/*
	b      c

	lower_bound of bXYZ gives c.

	lower_bound of b gives b

	upper_bound of bXYZ gives c

	upper_bound of b gives c, which is what we want.
	*/
	auto res = std::upper_bound(prefixes.begin(), prefixes.end(), URL); // "Returns an iterator pointing to the first element in the range [first, last) that is greater than value, or last if no such element is found."
	if(res == prefixes.begin())
		return false;
	--res;
	const std::string& prefix_string = *res;

	assert(StringUtils::containsChar(prefix_string, '/'));

	if(StringUtils::containsChar(prefix_string, '/'))
	{
		return ::hasPrefix(URL, prefix_string);
	}
	else
	{
		return false; // Shouldn't happen
	}
}


#include <TestUtils.h>


void URLWhitelist::test()
{
	// Test with empty whitelist
	{
		URLWhitelist whitelist;

		testAssert(!whitelist.isURLPrefixInWhitelist("a"));
	}

	// Test a whitelist with just a single entry
	{
		URLWhitelist whitelist;
		whitelist.prefixes.push_back("domain/b");

		testAssert(whitelist.isURLPrefixInWhitelist("domain/b"));
		testAssert(whitelist.isURLPrefixInWhitelist("domain/bXYZ"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain/a"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain/c"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain/"));
		testAssert(!whitelist.isURLPrefixInWhitelist(""));
	}

	{
		URLWhitelist whitelist;
		whitelist.prefixes.push_back("domain/b");
		whitelist.prefixes.push_back("domain/cc");
		whitelist.prefixes.push_back("domain/def");

		testAssert(whitelist.isURLPrefixInWhitelist("domain/b"));
		testAssert(whitelist.isURLPrefixInWhitelist("domain/bXYZ"));
		testAssert(whitelist.isURLPrefixInWhitelist("domain/cc"));
		testAssert(whitelist.isURLPrefixInWhitelist("domain/def"));
		testAssert(whitelist.isURLPrefixInWhitelist("domain/defXYZ"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain/a"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain/c"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain/deZZ"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain/z"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain/"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domain"));
		testAssert(!whitelist.isURLPrefixInWhitelist("domai"));
		testAssert(!whitelist.isURLPrefixInWhitelist(""));
	}

	// If there is no slash in the (scheme-stripped) URL prefix, then we just have a domain name.  In this case the whole domain name needs to match exactly.
	{
		URLWhitelist whitelist;
		whitelist.prefixes.push_back("some.domain/");

		testAssert(whitelist.isURLPrefixInWhitelist("some.domain/"));
		testAssert(whitelist.isURLPrefixInWhitelist("some.domain/b"));

		testAssert(!whitelist.isURLPrefixInWhitelist("some.domain.my.evil.site"));
		testAssert(!whitelist.isURLPrefixInWhitelist("some.domain.my.evil.site/"));
		testAssert(!whitelist.isURLPrefixInWhitelist("some.domain.my.evil.site/b"));

		testAssert(whitelist.isURLPrefixInWhitelist("http://some.domain/"));
		testAssert(whitelist.isURLPrefixInWhitelist("http://some.domain/b"));

		testAssert(!whitelist.isURLPrefixInWhitelist("http://some.domain.my.evil.site"));
		testAssert(!whitelist.isURLPrefixInWhitelist("http://some.domain.my.evil.site/"));
		testAssert(!whitelist.isURLPrefixInWhitelist("http://some.domain.my.evil.site/b"));
	}

	// If there is no slash in the (scheme-stripped) URL prefix, then we just have a domain name.  In this case the whole domain name needs to match exactly.
	{
		URLWhitelist whitelist;
		whitelist.loadDefaultWhitelist();

		testAssert(whitelist.isURLPrefixInWhitelist("youtube.com"));
		testAssert(whitelist.isURLPrefixInWhitelist("youtube.com/"));
		testAssert(whitelist.isURLPrefixInWhitelist("youtube.com/asdsddasd"));

		testAssert(whitelist.isURLPrefixInWhitelist("https://opensea.io/assets/0x495f947276749ce646f68ac8c248420045cb7b5e/74200906326078283434529278345955196052332239189126191305460846631806411210753"));
		testAssert(whitelist.isURLPrefixInWhitelist("opensea.io/assets/0x495f947276749ce646f68ac8c248420045cb7b5e/74200906326078283434529278345955196052332239189126191305460846631806411210753"));

		testAssert(!whitelist.isURLPrefixInWhitelist("opensea.iZ/assets/0x495f947276749ce646f68ac8c248420045cb7b5e/74200906326078283434529278345955196052332239189126191305460846631806411210753"));

		testAssert(!whitelist.isURLPrefixInWhitelist("youtube.com.my.evil.site"));
		testAssert(!whitelist.isURLPrefixInWhitelist("youtube.com.my.evil.site/"));
		testAssert(!whitelist.isURLPrefixInWhitelist("youtube.com.my.evil.site/asdsddasd"));

		testAssert(!whitelist.isURLPrefixInWhitelist("youtube.coZ/asdsddasd"));
	}
}
