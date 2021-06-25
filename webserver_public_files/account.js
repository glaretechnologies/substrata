// Copyright Glare Technologies Limited 2021 -


function accountConnectedSuccess(accounts) {
    var account = accounts[0];

    document.querySelector('.metamask-status-div').innerHTML = "Connected to account " + account;

    var ethereumButton = document.querySelector('.enableEthereumButton');
    ethereumButton.disabled = true;

    var signButton = document.querySelector('.signEthereumButton');
    signButton.disabled = false;
}


function accountConnectedFailure() {
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


// MetaMask (or similar) has succesfully signed a message
function requestSignMessageSuccess(result) {
    const request = new XMLHttpRequest();
    const url = '/account_eth_sign_message_post?address=' + ethereum.selectedAddress + '&sig=' + result;
    request.open("POST", url);
    
    request.onload = function () {
        if (request.readyState === request.DONE && request.status === 200) {

            var response_json = JSON.parse(request.responseText);

            var msg = response_json["msg"];
            var redirect_URL = response_json["redirect_URL"];

            if (msg != null)
                alert(msg);

            if (redirect_URL != null)
                window.location = redirect_URL;
        }
    };

    request.send();
}


function requestSignMessageFailure() {
}


function requestSignMessage() {
    try {
        var accounts = window.ethereum.request({
            method: 'personal_sign', 'params': [ethereum.selectedAddress, "Please sign this message to confirm you own the Ethereum account.\n(Unique string: " + current_eth_signing_nonce + ")"]
        }).then(requestSignMessageSuccess, requestSignMessageFailure);
    }
    catch (error) {
        console.error("Error while signing message: " + error);
    }
}
