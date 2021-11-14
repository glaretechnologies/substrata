var mymap = L.map('mapid', { crs: L.CRS.Simple }).setView([0.0, 0.0], 4);

L.tileLayer('/tile?x={x}&y={y}&z={z}', {
    zoomOffset: 0,
    minZoom: 0,
    maxZoom: 6,
    noWrap: true,
}).addTo(mymap);


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

for (var i = 0; i < poly_parcel_ids.length; i++) {

    var is_highlighted_parcel = poly_parcel_ids[i] == highlight_parcel_id;

    var vert_i = i * 4;
    var polygon = L.polygon([
        poly_coords[vert_i + 0],
        poly_coords[vert_i + 1],
        poly_coords[vert_i + 2],
        poly_coords[vert_i + 3],
    ], {
        weight: 2, // line width in pixels
        color: (is_highlighted_parcel ? '#ef9518' : '#3388ff'),
        //fillColor: '#777',
        fillOpacity: (is_highlighted_parcel ? 0.2 : 0.05)
    }).addTo(mymap);

    var popup = polygon.bindPopup("<a href=\"/parcel/" + poly_parcel_ids[i].toString() + "\">Parcel " + poly_parcel_ids[i].toString() + "</a>");

    if (is_highlighted_parcel)
        popup.openPopup();
}


for (var i = 0; i < rect_parcel_ids.length; i++) {

    var is_highlighted_parcel = rect_parcel_ids[i] == highlight_parcel_id;

    let margin = 0.01

    var coord_i = i * 4;
    var polygon = L.rectangle(
        [[rect_bound_coords[coord_i + 0] + margin, rect_bound_coords[coord_i + 1] + margin], [rect_bound_coords[coord_i + 2] - margin, rect_bound_coords[coord_i + 3] - margin]],
        {
        weight: 2, // line width in pixels
        color: (is_highlighted_parcel ? '#ef9518' : '#3388ff'),
        //fillColor: '#777',
        fillOpacity: (is_highlighted_parcel ? 0.2 : 0.05)
    }).addTo(mymap);

    var popup = polygon.bindPopup("<a href=\"/parcel/" + rect_parcel_ids[i].toString() + "\">Parcel " + rect_parcel_ids[i].toString() + "</a>");

    if (is_highlighted_parcel)
        popup.openPopup();
}

