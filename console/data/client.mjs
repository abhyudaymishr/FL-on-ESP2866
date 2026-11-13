export class TelemetryClient {
  constructor({token='',fetcher=fetch}={}) {this.token=token;this.fetcher=fetcher;}
  async get(path,signal) {
    const response=await this.fetcher(path,{headers:{Authorization:`Bearer ${this.token}`},cache:'no-store',signal});
    if(response.status===401)throw new Error('Read token was not accepted. Open connection settings.');
    if(!response.ok)throw new Error(`Telemetry service returned ${response.status}.`);
    return response.json();
  }
  state(signal) {return this.get('/api/state',signal);}
  history(minutes,signal) {return this.get(`/api/history?minutes=${minutes}`,signal);}
}
