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

for (var i = 0; i < parcel_ids.length; i++) {
    var vert_i = i * 4;
    var polygon = L.polygon([
        poly_coords[vert_i + 0],
        poly_coords[vert_i + 1],
        poly_coords[vert_i + 2],
        poly_coords[vert_i + 3],
    ], {
        weight: 2, // line width in pixels
        //color: '#777',
        //fillColor: '#777',
        fillOpacity: 0.05
    }).addTo(mymap);

    polygon.bindPopup("<a href=\"/parcel/" + parcel_ids[i].toString() + "\">Parcel " + parcel_ids[i].toString() + "</a>");
}
