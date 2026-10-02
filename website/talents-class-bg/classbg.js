/* Centurion: the talent calculator's background follows the selected class.
   The app routes by hash (#/druid, #/rogue/..., #/ for the class picker); each class has
   its own art in bg/, and the picker shows default.jpg. The other images are preloaded
   once the page is idle, so switching classes swaps instantly. */
(function () {
	var BASE = '/legionnaireplus-talent-calculator/bg/';
	var VERSION = '3';
	var CLASSES = ['druid', 'hunter', 'mage', 'paladin', 'priest', 'rogue', 'shaman', 'warlock', 'warrior'];

	function current() {
		var seg = (location.hash || '').replace(/^#\/?/, '').split(/[\/?&]/)[0].toLowerCase();
		return CLASSES.indexOf(seg) >= 0 ? seg : 'default';
	}

	function apply() {
		document.documentElement.style.backgroundImage =
			'url(' + BASE + current() + '.jpg?v=' + VERSION + ')';
	}

	window.addEventListener('hashchange', apply);
	window.addEventListener('popstate', apply);
	apply();

	window.addEventListener('load', function () {
		setTimeout(function () {
			CLASSES.forEach(function (c) { new Image().src = BASE + c + '.jpg?v=' + VERSION; });
		}, 1500);
	});
})();
