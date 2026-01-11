let mymap = L.map('mapid', { crs: L.CRS.Simple }).setView([0.0, 0.0], 4);

L.tileLayer('/tile?x={x}&y={y}&z={z}', {
    zoomOffset: 0,
    minZoom: 0,
    maxZoom: 6,
    noWrap: true,
}).addTo(mymap);


var info = L.control();

info.onAdd = function (map) {
    this._div = L.DomUtil.create('div', 'map-info'); // create a div with a class "info"
    this.update();
    return this._div;
};


const MRADMIN_COL = '#3388ff'; // blue
const OTHER_OWNED_COL = '#0f9caa'; // cyan
const FOR_AUCTION_COL = '#b40d96'; // purpleish
const HIGHLIGHTED_COL = '#ef9518';


// method that we will use to update the control based on feature properties passed
info.update = function (props) {
    this._div.innerHTML =
        "<span class=\"map-col-mradmin\">&#9632;</span> Owned by Substrata<br/>" +
        "<span class=\"map-col-other\">&#9632;</span> Owned by other<br/>" +
        "<span class=\"map-col-for-auction\">&#9632;</span> Currently at auction";

};

info.addTo(mymap);



/*mymap.on('click', function (e) {
    console.log("lat: " + e.latlng.lat);
    console.log("lng: " + e.latlng.lng);
});*/


/*L.GridLayer.GridDebug = L.GridLayer.extend({
    createTile: function (coords) {
        const tile = document.createElement('div');
        tile.style.outline = '1px solid green';
        tile.style.fontWeight = 'bold';
        tile.style.fontSize = '14pt';
        tile.innerHTML = [coords.x, coords.y, coords.z].join('/');
        return tile;
    },
});

L.gridLayer.gridDebug = function (opts) {
    return new L.GridLayer.GridDebug(opts);
};

mymap.addLayer(L.gridLayer.gridDebug());*/


var elems = document.getElementById("poly_coords").textContent.split(',');
var poly_coords = []
for (let i = 0; i < elems.length; i += 2) {
    poly_coords.push([elems[i], elems[i + 1]]);
}

var poly_parcel_ids = document.getElementById("poly_parcel_ids").textContent.split(',').map(s => parseInt(s, /*radix=*/10));

var poly_parcel_names = document.getElementById("poly_parcel_names").textContent.split(';');

var poly_parcel_state = document.getElementById("poly_parcel_state").textContent.split(',').map(s => parseInt(s, /*radix=*/10));

var rect_bound_coords = document.getElementById("rect_bound_coords").textContent.split(',').map(s => parseFloat(s));

var rect_parcel_ids = document.getElementById("rect_parcel_ids").textContent.split(',').map(s => parseInt(s, /*radix=*/10));

var rect_parcel_names = document.getElementById("rect_parcel_names").textContent.split(';');

var rect_parcel_state = document.getElementById("rect_parcel_state").textContent.split(',').map(s => parseInt(s, /*radix=*/10));

var highlight_parcel_id = parseInt(document.getElementById("highlight_parcel_id").textContent, /*radix=*/10);

function escapeHtml(unsafe) {
    return unsafe
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}


for (let i = 0; i < poly_parcel_ids.length; i++) {

    let is_highlighted_parcel = poly_parcel_ids[i] == highlight_parcel_id;
    let state = poly_parcel_state[i];

    // 0 = owned by MrAdmin and not on auction, 1 = owned by MrAdmin and for auction, 2 = owned by someone else
    let colour = null;
    if (state == 0)
        colour = MRADMIN_COL
    else if (state == 1)
        colour = FOR_AUCTION_COL
    else
        colour = OTHER_OWNED_COL

    if (is_highlighted_parcel)
        colour = HIGHLIGHTED_COL

    let vert_i = i * 4;
    let polygon = L.polygon([
        poly_coords[vert_i + 0],
        poly_coords[vert_i + 1],
        poly_coords[vert_i + 2],
        poly_coords[vert_i + 3],
    ], {
        weight: 2, // line width in pixels
        color: colour,
        //fillColor: '#777',
        fillOpacity: (is_highlighted_parcel ? 0.2 : 0.05)
    }).addTo(mymap);

    let title = (poly_parcel_names[i].length == 0) ? ("Parcel " + poly_parcel_ids[i].toString()) : escapeHtml(poly_parcel_names[i]);

    let popup = polygon.bindPopup("<a href=\"/parcel/" + poly_parcel_ids[i].toString() + "\">" + title + "</a>");

    if (is_highlighted_parcel)
        popup.openPopup();
}


for (let i = 0; i < rect_parcel_ids.length; i++) {

    let is_highlighted_parcel = rect_parcel_ids[i] == highlight_parcel_id;
    let state = rect_parcel_state[i];

    let margin = 0.01

    // 0 = owned by MrAdmin and not on auction, 1 = owned by MrAdmin and for auction, 2 = owned by someone else
    let colour = null;
    if (state == 0)
        colour = MRADMIN_COL
    else if (state == 1)
        colour = FOR_AUCTION_COL
    else
        colour = OTHER_OWNED_COL

    if (is_highlighted_parcel)
        colour = HIGHLIGHTED_COL;

    let coord_i = i * 4;
    let polygon = L.rectangle(
        [[rect_bound_coords[coord_i + 0] + margin, rect_bound_coords[coord_i + 1] + margin], [rect_bound_coords[coord_i + 2] - margin, rect_bound_coords[coord_i + 3] - margin]],
        {
        weight: 2, // line width in pixels
        color: colour,
        //fillColor: '#777',
        fillOpacity: (is_highlighted_parcel ? 0.2 : 0.05)
    }).addTo(mymap);

    let title = (rect_parcel_names[i].length == 0) ? ("Parcel " + rect_parcel_ids[i].toString()) : escapeHtml(rect_parcel_names[i]);

    let popup = polygon.bindPopup("<a href=\"/parcel/" + rect_parcel_ids[i].toString() + "\">" + title + "</a>");

    if (is_highlighted_parcel)
        popup.openPopup();
}
