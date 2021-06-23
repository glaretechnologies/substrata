// Copyright Glare Technologies Limited 2021 -


function accountConnectedSuccess(accounts) {
    //alert("accountConnectedSuccess");

    //console.log(accounts);

    var account = accounts[0];

    //console.log(account);

    document.querySelector('.metamask-status-div').innerHTML = "Connected to account " + account;

    var ethereumButton = document.querySelector('.enableEthereumButton');
    ethereumButton.disabled = true;

    var signButton = document.querySelector('.signEthereumButton');
    signButton.disabled = false;
}

function accountConnectedFailure() {
   // alert("accountConnectedFailure");

    document.querySelector('.metamask-status-div').innerHTML = "Not connected";
}

function connectToMetaMaskAccount() {

    try {
        var accounts = window.ethereum.request({ method: 'eth_requestAccounts' }).then(accountConnectedSuccess, accountConnectedFailure);
    }
    catch (error) {
        console.error("Error while connecting to Metamask account: " + error);
    }
}


function requestSignMessageSuccess(result) {
   // alert("requestSignMessageSuccess");

    console.log("result: " + result);

    console.log("Making XMLHttpRequest...");
    const request = new XMLHttpRequest();
    const url = '/account_eth_sign_message_post?address=' + ethereum.selectedAddress + '&sig=' + result;
    request.open("POST", url);
    

    console.log("XMLHttpRequest launched...");

    request.onload = function () {
        if (request.readyState === request.DONE && request.status === 200) {
            //console.log(xhr.response, xhr.responseXML);

            //alert("request.responseText: " + request.responseText);

            var response_json = JSON.parse(request.responseText);

            var msg = response_json["msg"];
            var redirect_URL = response_json["redirect_URL"];

            if (msg != null)
                alert(msg);
           // alert("redirect_URL: " + redirect_URL);

            if (redirect_URL != null)
                window.location = redirect_URL;
        }
    };

    request.send();
}

function requestSignMessageFailure() {
    //alert("requestSignMessageFailure");
}


function requestSignMessage() {
    //alert("current_eth_signing_nonce: " + current_eth_signing_nonce);
    try {
        var accounts = window.ethereum.request({
            method: 'personal_sign', 'params': [ethereum.selectedAddress, "Please sign this message to confirm you own the Ethereum account.\n(Unique string: " + current_eth_signing_nonce + ")"]
        }).
            then(requestSignMessageSuccess, requestSignMessageFailure);
    }
    catch (error) {
        console.error("Error while signing message: " + error);
    }
}

